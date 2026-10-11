import argparse
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout

path = Path(__file__).resolve().parents[1] / 'tools/render/run_transition_control.py'
spec = importlib.util.spec_from_file_location('transition_control', path)
control = importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)


class FakeWindows:
    def __init__(self, minimized=False, foreground=2):
        self.is_minimized, self.foreground_window = minimized, foreground
        self.detail_reads = []

    def windows(self):
        return [1, 2, 3]

    def pid(self, handle):
        return {1: 123, 2: 456, 3: 123}[handle]

    def visible(self, handle):
        self.detail_reads.append(('visible', handle))
        return handle != 3

    def minimized(self, handle):
        self.detail_reads.append(('minimized', handle))
        return self.is_minimized

    def foreground(self):
        return self.foreground_window


class FakeChild:
    def __init__(self, returncode=None, interrupt=False, stall_on_stop=False):
        self.returncode = returncode
        self.interrupt, self.stall_on_stop = interrupt, stall_on_stop
        self.terminated = self.killed = 0

    def poll(self):
        return self.returncode

    def wait(self, timeout=None):
        if self.interrupt:
            self.interrupt = False
            raise KeyboardInterrupt
        if self.returncode is None and (not self.terminated or self.stall_on_stop):
            raise subprocess.TimeoutExpired('procedural_child', timeout)
        if self.returncode is None:
            self.returncode = -1
        return self.returncode

    def terminate(self):
        self.terminated += 1

    def kill(self):
        self.killed += 1
        self.returncode = -2


class TransitionControlTest(unittest.TestCase):
    def fixture(self, root):
        runner, elf, iso, card = (root/name for name in ('runner', 'elf', 'iso', 'card'))
        for path in (runner, elf, iso, card):
            path.write_bytes(b'procedural-'+path.name.encode())
        modules = root/'modules'
        modules.mkdir()
        (modules/'procedural.txt').write_text('sin datos del juego', encoding='utf-8')
        manifest = root/'build.json'
        manifest.write_text(json.dumps(dict(commit='a'*40, exe_sha256=control.digest(runner),
                                            elf_sha256=control.digest(elf))), encoding='utf-8')
        return argparse.Namespace(case='sync_ram', label=None, seconds=360, runner=runner,
                                  elf=elf, iso=iso, iop_mod=modules, build_manifest=manifest,
                                  output_root=root/'output', initial_card=card, collapsed_ui=False,
                                  vu1_thread='0', gs_front='0', diag_from_tick=0, capture_acquired=0)

    def test_environment_drops_presence_diagnostics_and_preserves_os_variables(self):
        settings = control.settings_for('async_shared', Path('/case'), Path('/iso'), Path('/cache'))
        env = control.isolated_environment(
            {'PATH': 'procedural', 'gow_anm_diag': '0', 'PS2X_GS_DISCARD_DRAWS': '1',
             'GOW_GS_REPLAY_TRACE': 'old'}, settings)
        self.assertEqual(env['PATH'], 'procedural')
        self.assertNotIn('gow_anm_diag', env)
        self.assertNotIn('PS2X_GS_DISCARD_DRAWS', env)
        self.assertNotIn('GOW_GS_REPLAY_TRACE', env)
        self.assertEqual((env['GOW_GS_FINISH_ASINCRONO'], env['PS2X_GS_DIRECT_PRESENT']), ('1', '1'))
        self.assertEqual(env['GOW_PAD_SOLO_GUION'], '1')
        self.assertEqual((env['PS2X_VU1_HILO'], env['PS2X_GS_FRENTE']), ('0', '0'))

    def test_matrix_and_invalid_thread_combination(self):
        for case, pair in zip(control.CASES, [('0', '0'), ('0', '1'), ('1', '0'), ('1', '1')]):
            settings = control.settings_for(case, Path('/case'), Path('/iso'), Path('/cache'))
            self.assertEqual((settings['GOW_GS_FINISH_ASINCRONO'], settings['PS2X_GS_DIRECT_PRESENT']), pair)
        with self.assertRaises(ValueError):
            control.settings_for('async_shared', Path('/case'), Path('/iso'), Path('/cache'), '0', '1')

    def test_acquired_capture_is_explicit_and_inherited_capture_does_not_enable_it(self):
        settings = control.settings_for('async_shared', Path('/case'), Path('/iso'), Path('/cache'))
        self.assertFalse(any('ACQUIRED_CAPTURE' in key for key in settings))
        env = control.isolated_environment({'PS2X_GS_ACQUIRED_CAPTURE_DIR': '/old'}, settings)
        self.assertNotIn('PS2X_GS_ACQUIRED_CAPTURE_DIR', env)

    def test_acquired_capture_gets_a_fresh_private_folder_and_records_its_window(self):
        with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
            args = self.fixture(Path(name))
            args.case, args.capture_acquired, args.diag_from_tick = 'async_shared', 6, 6000
            with patch.object(control, 'EXPECTED_ELF_SHA256', control.digest(args.elf)):
                folder, _, _, result = control.prepare_case(args)
            self.assertEqual(result['settings']['PS2X_GS_ACQUIRED_CAPTURE_DIR'], str(folder/'acquired'))
            self.assertEqual(result['settings']['PS2X_GS_ACQUIRED_CAPTURE_COUNT'], '6')
            self.assertEqual(result['settings']['PS2X_GS_ACQUIRED_CAPTURE_FROM_TICK'], '6000')
            self.assertTrue((folder/'acquired').is_dir())
            self.assertFalse(any((folder/'acquired').iterdir()))

    def test_invalid_capture_budget_or_ram_case_is_rejected_before_creating_output(self):
        for invalid in (-1, 17, True, '6'):
            with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
                args = self.fixture(Path(name))
                args.case, args.capture_acquired = 'async_shared', invalid
                with patch.object(control, 'EXPECTED_ELF_SHA256', control.digest(args.elf)):
                    with self.assertRaises(ValueError):
                        control.prepare_case(args)
                self.assertFalse(args.output_root.exists())
        with self.assertRaises(ValueError):
            control.settings_for('async_ram', Path('/case'), Path('/iso'), Path('/cache'), capture_acquired=6)

    def test_losing_focus_does_not_imply_minimization_or_read_another_window(self):
        api = FakeWindows()
        state = control.window_state(api, 123)
        self.assertEqual(state, dict(window_found=True, visible=True, minimized=False, foreground=False))
        self.assertEqual(api.detail_reads, [('visible', 1), ('minimized', 1)])
        api.is_minimized, api.foreground_window = True, 1
        state = control.window_state(api, 123)
        self.assertTrue(state['minimized'])
        self.assertTrue(state['foreground'])
        self.assertNotIn(('minimized', 2), api.detail_reads)

    def test_deferred_window_is_recorded_and_invalid_ticks_do_not_create_a_pass(self):
        with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
            args = self.fixture(Path(name))
            args.diag_from_tick = 6000
            with patch.object(control, 'EXPECTED_ELF_SHA256', control.digest(args.elf)):
                _, _, _, result = control.prepare_case(args)
            self.assertEqual(result['settings']['PS2X_GS_PRESENT_DIAG_FROM_TICK'], '6000')
        for invalid in (-1, 1 << 64, '6000', True):
            with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
                args = self.fixture(Path(name))
                args.diag_from_tick = invalid
                with patch.object(control, 'EXPECTED_ELF_SHA256', control.digest(args.elf)):
                    with self.assertRaises(ValueError):
                        control.prepare_case(args)
                self.assertFalse(args.output_root.exists())

    def test_missing_owned_window_is_unknown_not_minimized(self):
        api = FakeWindows()
        state = control.window_state(api, 789)
        self.assertFalse(state['window_found'])
        self.assertIsNone(state['minimized'])
        self.assertFalse(state['foreground'])
        self.assertEqual(api.detail_reads, [])

    def test_card_copy_and_repeated_pass_never_overwrite_source(self):
        with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
            root = Path(name)
            args = self.fixture(root)
            original = args.initial_card.read_bytes()
            with patch.object(control, 'EXPECTED_ELF_SHA256', control.digest(args.elf)):
                folder, _, card, result = control.prepare_case(args)
                self.assertEqual(card.read_bytes(), original)
                card.write_bytes(b'procedural modified copy')
                with self.assertRaises(FileExistsError):
                    control.prepare_case(args)
                args.label = 'second_pass'
                _, _, second_card, _ = control.prepare_case(args)
                self.assertEqual(second_card.read_bytes(), original)
            self.assertEqual(args.initial_card.read_bytes(), original)
            self.assertEqual(result['settings']['GOW_MC0'], str(folder/'Mcd001.ps2'))
            self.assertEqual(result['settings']['GOW_MC1'], '0')

    def test_manifest_mismatch_fails_before_creating_pass(self):
        with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
            args = self.fixture(Path(name))
            args.runner.write_bytes(b'different procedural executable')
            with self.assertRaises(ValueError):
                control.prepare_case(args)
            self.assertFalse(args.output_root.exists())

    def test_manifest_accepts_uppercase_hashes_and_rejects_malformed_values(self):
        with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
            args = self.fixture(Path(name))
            build = json.loads(args.build_manifest.read_text(encoding='utf-8'))
            build['exe_sha256'] = build['exe_sha256'].upper()
            build['elf_sha256'] = build['elf_sha256'].upper()
            args.build_manifest.write_text(json.dumps(build), encoding='utf-8')
            with patch.object(control, 'EXPECTED_ELF_SHA256', control.digest(args.elf)):
                control.prepare_case(args)
        for invalid in (None, 12, 'a'*63, 'g'*64):
            with self.assertRaises(ValueError):
                control.manifest_digest({'exe_sha256': invalid}, 'exe_sha256')

    def test_invalid_label_and_recursive_module_copy_are_rejected_before_writes(self):
        with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
            args = self.fixture(Path(name))
            args.label = '../outside'
            with self.assertRaises(ValueError):
                control.prepare_case(args)
            args.label = None
            args.output_root = args.iop_mod/'output'
            with patch.object(control, 'EXPECTED_ELF_SHA256', control.digest(args.elf)):
                with self.assertRaises(ValueError):
                    control.prepare_case(args)
            self.assertFalse(args.output_root.exists())

    def test_limit_terminates_the_child_and_kills_only_if_it_does_not_exit(self):
        child = FakeChild(stall_on_stop=True)
        with patch.object(control.time, 'monotonic', side_effect=[0, 2]):
            reason = control.wait_for_control(child, 1, Path('/missing_procedural_stop_file'))
        self.assertEqual(reason, 'limit')
        self.assertEqual((child.terminated, child.killed), (1, 1))

    def test_checkout_output_is_rejected_before_writes_and_logs_remains_private(self):
        repo = path.parents[2]
        with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
            args = self.fixture(Path(name))
            args.output_root = repo/'procedural_transition_output_forbidden_guard'
            self.assertFalse(args.output_root.exists())
            with patch.object(control, 'EXPECTED_ELF_SHA256', control.digest(args.elf)), \
                    patch.object(Path, 'mkdir', side_effect=AssertionError('No crear la salida rechazada')):
                with self.assertRaises(ValueError):
                    control.prepare_case(args)
            self.assertFalse(args.output_root.exists())
        self.assertEqual(control.private_output(repo/'logs'/'procedural_transition_allowed'),
                         repo/'logs'/'procedural_transition_allowed')
        with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
            self.assertEqual(control.private_output(Path(name)), Path(name).resolve())

    def test_stop_file_and_keyboard_interrupt_are_recorded_as_deliberate(self):
        with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
            stop = Path(name)/'STOP'
            stop.touch()
            child = FakeChild()
            self.assertEqual(control.wait_for_control(child, 1, stop), 'stop_file')
            self.assertEqual((child.terminated, child.killed), (1, 0))
            stop.unlink()
            child = FakeChild(interrupt=True)
            self.assertEqual(control.wait_for_control(child, 1, stop), 'keyboard_interrupt')
            self.assertEqual((child.terminated, child.killed), (1, 0))

    def test_spontaneous_failure_is_preserved_and_finished_child_is_not_terminated(self):
        child = FakeChild(returncode=17)
        reason = control.wait_for_control(child, 1, Path('/missing_procedural_stop_file'))
        self.assertEqual(reason, 'process_exit')
        self.assertEqual(child.returncode, 17)
        self.assertEqual((child.terminated, child.killed), (0, 0))

    @unittest.skipUnless(os.name == 'nt', 'El lanzador completo usa STARTUPINFO y Win32')
    def test_windows_launcher_records_a_procedural_child_without_game_or_window(self):
        with tempfile.TemporaryDirectory(prefix='gow_transition_test_') as name:
            root = Path(name)
            args = self.fixture(root)
            # Python ejecuta una entrada procedural que termina enseguida; nunca arrancar el juego.
            args.runner = Path(sys.executable)
            args.elf.write_text('import os\nassert "GOW_GS_REPLAY_TRACE" not in os.environ\n'
                                'assert os.environ["GOW_PAD_SOLO_GUION"] == "1"\n'
                                'assert os.environ["PS2X_GS_PRESENT_DIAG_FROM_TICK"] == "6000"\n'
                                'print("procedural child: OK")\n', encoding='utf-8')
            expected = control.digest(args.elf)
            args.build_manifest.write_text(json.dumps(dict(
                commit='a'*40, exe_sha256=control.digest(args.runner), elf_sha256=expected)),
                encoding='utf-8')
            argv = [str(path), args.case, '--seconds', '1', '--diag-from-tick', '6000']
            for option in ('runner', 'elf', 'iso', 'iop_mod', 'build_manifest', 'output_root', 'initial_card'):
                argv += ['--'+option.replace('_', '-'), str(getattr(args, option))]
            with patch.object(control, 'EXPECTED_ELF_SHA256', expected), patch.object(sys, 'argv', argv):
                with redirect_stdout(io.StringIO()):
                    self.assertEqual(control.main(), 0)
            folder = args.output_root/args.case
            result = json.loads((folder/'result.json').read_text(encoding='utf-8'))
            self.assertEqual((result['exit_code'], result['stop_reason']), (0, 'process_exit'))
            self.assertEqual(result['initial_card_sha256'], result['final_card_sha256'])
            self.assertIn('procedural child: OK', (folder/'out.log').read_text(encoding='utf-8'))
            self.assertTrue((folder/'window_state.jsonl').is_file())


if __name__ == '__main__':
    unittest.main()
