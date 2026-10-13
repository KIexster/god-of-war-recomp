"""No confundir FPS de carga con partida ni igualar relojes distintos."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/rendimiento/resumir.py'
spec = importlib.util.spec_from_file_location('perf_summary', SCRIPT)
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


def perf(t, window=5):
    return f'[gow-perf] t={t:.2f} window={window:.2f} guest_flip_hz=10.00 host_hz=60.00 ee_ms=500.00'


def state(value=11, pending=0, ready=1):
    # El reloj del mando difiere intencionalmente del reloj del perfil.
    return f'[gow-pad2:state] seconds=700.00 state={value} pending={pending} levelReady={ready}'


def sample():
    return '\n'.join([perf(5), state(3, 1, 0), perf(10), state(),
                      perf(15), state(), perf(20), state(), perf(25), state(4, 1, 0)])


class PerfSummaryTest(unittest.TestCase):
    def test_disabled_timers_do_not_claim_no_wait(self):
        row = dict(t=5, window=5, guest_flip_hz=30, host_hz=12,
                   **{key: 0 for key in summary.TIME_KEYS})
        data = summary.summarize_windows([row])
        self.assertEqual((data['guest_flip_hz'], data['host_hz']), (30, 12))
        self.assertIsNone(data['tiempo_ms'])
        self.assertIsNone(data['reparto_tiempo_instrumentado_pct'])
        self.assertIn('No acredita ausencia', data['limite_tiempos'])

    def test_threaded_scopes_are_not_named_a_game_thread_percentage(self):
        row = dict(t=5, window=5, guest_flip_hz=30, host_hz=12,
                   ee_ms=3000, iop_ms=2000, vu_ms=4000, gs_ms=1000,
                   guest_wait_ms=0, upload_ms=100, host_wait_ms=4900)
        data = summary.summarize_windows([row])
        self.assertNotIn('porcentaje_hilo_juego', data)
        self.assertAlmostEqual(sum(data['reparto_tiempo_instrumentado_pct'].values()), 100)
        self.assertEqual(data['tiempo_ms']['host_wait_ms'], 4900)
        self.assertIn('No uso de CPU', data['limite_tiempos'])
        self.assertIn('pueden repetir', data['metricas']['host_hz'])

    def test_absent_scope_is_unknown_and_absent_counter_is_invalid(self):
        data = summary.summarize_windows(summary.select_windows(perf(5), 0, 10))
        self.assertIsNone(data['tiempo_ms']['host_wait_ms'])
        self.assertIsNone(data['reparto_tiempo_instrumentado_pct']['host_wait_ms'])
        with self.assertRaises(ValueError):
            summary.select_windows('[gow-perf] t=5 window=5 guest_flip_hz=30', 0, 10)

    def test_malformed_numeric_windows_fail_before_export(self):
        for text in (perf(5, 0), perf(5).replace('host_hz=60.00', 'host_hz=nan'),
                     perf(5).replace('host_hz=60.00', 'host_hz=-1'),
                     perf(5)+' host_hz=1', '\n'.join([perf(5), perf(5)]),
                     '\n'.join([perf(5), perf(9)])):
            with self.subTest(text=text), self.assertRaises(ValueError):
                summary.select_windows(text, 0, 100)

    def test_partida_excludes_transition_windows_with_different_clocks(self):
        self.assertEqual([row['t'] for row in summary.select_windows(sample(), 0, 100, True)], [20])

    def test_loading_is_rejected(self):
        text = '\n'.join([state(3, 1, 0), perf(5), perf(10), state(3, 1, 0)])
        self.assertEqual(summary.select_windows(text, 0, 100, True), [])

    def test_pending_and_unready_split_loaded_runs(self):
        for invalid in [state(pending=1), state(ready=0)]:
            text = '\n'.join([state(), perf(5), invalid, perf(10), state(), perf(15), state()])
            self.assertEqual(summary.select_windows(text, 0, 100, True), [])

    def test_needs_both_state_bounds(self):
        for text in ['\n'.join([state(), perf(5), perf(10)]),
                     '\n'.join([perf(5), perf(10), state()]), '\n'.join([perf(5), perf(10)])]:
            self.assertEqual(summary.select_windows(text, 0, 100, True), [])

    def test_interval_and_normal_mode_are_preserved(self):
        self.assertEqual([row['t'] for row in summary.select_windows(sample(), 10, 20)], [15, 20])
        self.assertEqual(summary.select_windows(sample(), 20, 30, True), [])

    def test_invalidated_run_is_rejected_even_outside_selected_interval(self):
        marker = '[gow-perf:invalid] Perfil invalidado por carga externa: cl.'
        for loaded in (False, True):
            for text in (marker+'\n'+sample(), sample()+'\n'+marker):
                with self.assertRaisesRegex(ValueError, 'perfil invalidado'):
                    summary.select_windows(text, 10, 20, loaded)

    def test_cli_does_not_export_a_summary_for_invalidated_run(self):
        with tempfile.TemporaryDirectory() as folder:
            log, output = Path(folder)/'perfil.log', Path(folder)/'resumen.json'
            log.write_text(sample()+'\n[gow-perf:invalid] otra partida', encoding='utf-8')
            result = subprocess.run([sys.executable, str(SCRIPT), str(log), '--desde', '0',
                                     '--hasta', '100', '--json', str(output)],
                                    capture_output=True, text=True, encoding='utf-8',
                                    env={**os.environ, 'PYTHONIOENCODING': 'utf-8'})
            self.assertEqual(result.returncode, 2)
            self.assertIn('perfil invalidado', result.stderr)
            self.assertFalse(output.exists())

    def test_separately_rounded_times_do_not_drop_full_window(self):
        text = '\n'.join([state(), perf(5.01), state(), perf(10.01, 5.01), state()])
        self.assertEqual([row['t'] for row in summary.select_windows(text, 0, 20, True)], [10.01])

    def test_cli_reports_loaded_interval_and_rejects_loading(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'perfil.log'
            log.write_text(sample(), encoding='utf-8')
            command = [sys.executable, str(SCRIPT), str(log), '--desde', '0', '--hasta', '100', '--partida']
            result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(result.stdout)
            self.assertEqual((data['ventanas'], data['desde'], data['hasta'], data['guest_flip_hz']),
                             (1, 15, 20, 10))
            log.write_text('\n'.join([state(3, 1, 0), perf(5), perf(10), state(3, 1, 0)]), encoding='utf-8')
            result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(result.returncode, 2)
            self.assertIn('partida cargada', result.stderr)


if __name__ == '__main__':
    unittest.main()
