import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / 'tools/render/inspect_shared_frames.py'
spec = importlib.util.spec_from_file_location('shared_frames', path)
frames = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frames)

def frame(seq, hash_value, valid=1, host=10, width=640):
    return f'[gs-present:shared] seq={seq} render={seq} hostTick={host} frameTick=8 displayFbp=208 sourceFbp=208 width={width} height=448 hashValid={valid} hash={hash_value:016x} blank=0'

class SharedFramesTest(unittest.TestCase):
    def test_content_return_with_increasing_sequence(self):
        result = frames.inspect('\n'.join([frame(1, 11), frame(2, 22), frame(3, 11)]))
        self.assertEqual(len(result['content_returns']), 1)
        self.assertEqual(result['sequence_regressions'], [])

    def test_retaining_texture_is_not_new_acquisition(self):
        result = frames.inspect('\n'.join([frame(1, 11), frame(1, 11, host=11), frame(2, 22)]))
        self.assertEqual((result['acquisitions'], result['retained']), (2, 1))
        self.assertEqual(result['inconsistent_repeats'], [])

    def test_same_sequence_cannot_change_content(self):
        result = frames.inspect('\n'.join([frame(1, 11), frame(1, 22)]))
        self.assertEqual(result['inconsistent_repeats'], [2])

    def test_nonconsecutive_sequence_keeps_its_original_metadata(self):
        for changed in [frame(1, 33), frame(1, 11, valid=0), frame(1, 11, width=512),
                        frame(1, 11).replace('render=1', 'render=3')]:
            with self.subTest(changed=changed):
                result = frames.inspect('\n'.join([frame(1, 11), frame(2, 22), changed,
                                                  frame(3, 22), frame(4, 33)]))
                self.assertEqual(result['inconsistent_repeats'], [3])
                self.assertEqual(result['sequence_regressions'], [3])
                self.assertEqual(result['content_returns'], [])

    def test_reappearing_sequence_allows_host_tick_and_mode_changes(self):
        result = frames.inspect('\n'.join([frame(1, 11), frame(2, 22), frame(1, 11, host=11)]))
        self.assertEqual(result['inconsistent_repeats'], [])
        self.assertEqual(result['sequence_regressions'], [3])
        text = frame(1, 11)+'\n[gs-present] host mode: RAM frames\n'+frame(1, 33)
        result = frames.inspect(text)
        self.assertEqual(result['inconsistent_repeats'], [])
        self.assertEqual(result['sequence_regressions'], [])

    def test_sequence_regression_and_mode_reset(self):
        self.assertEqual(frames.inspect(frame(2, 22)+'\n'+frame(1, 11))['sequence_regressions'], [2])
        text = frame(1, 11)+'\n'+frame(2, 22).replace('render=2', 'render=0')
        self.assertEqual(frames.inspect(text)['sequence_regressions'], [2])
        text = frame(2, 22)+'\n[gs-present] host mode: RAM frames\n'+frame(1, 11)
        self.assertEqual(frames.inspect(text)['sequence_regressions'], [])

    def test_unknown_hash_and_dimensions_break_false_match(self):
        for middle in [frame(2, 22, valid=0), frame(2, 11)]:
            self.assertEqual(frames.inspect('\n'.join([frame(1, 11), middle, frame(3, 11)]))['content_returns'], [])
        self.assertEqual(frames.inspect('\n'.join([frame(1, 11), frame(2, 22), frame(3, 11, width=512)]))['content_returns'], [])
        self.assertEqual(frames.inspect('\n'.join([frame(1, 11), frame(2, 22, width=512), frame(3, 11)]))['content_returns'], [])
        text = '\n'.join([frame(1, 11), frame(2, 22), frame(2, 33), frame(3, 11)])
        self.assertEqual(frames.inspect(text)['content_returns'], [])

    def test_missing_or_corrupt_logs_are_rejected(self):
        for text in ['', 'otro registro', frame(1, 11).rsplit(' ', 1)[0], frame(1, 11).replace('hashValid=1', 'hashValid=2'), frame(1, 11).replace('seq=1', 'seq=-1')]:
            with self.assertRaises(ValueError):
                frames.inspect(text)

if __name__ == '__main__':
    unittest.main()
