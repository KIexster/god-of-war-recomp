import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / 'tools/render/inspect_shared_frames.py'
spec = importlib.util.spec_from_file_location('shared_frames', path)
frames = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frames)

def frame(seq, hash_value, valid=1, host=10, width=640, preferred=None):
    text = f'[gs-present:shared] seq={seq} render={seq} hostTick={host} frameTick=8 displayFbp=208 sourceFbp=208 width={width} height=448 hashValid={valid} hash={hash_value:016x} blank=0'
    return text if preferred is None else text+f' preferred={preferred}'

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

    def test_tick_distance_preserves_the_record_with_the_largest_gap(self):
        text = '\n'.join([frame(1, 11, host=10),
                          frame(2, 22, host=1365).replace('frameTick=8', 'frameTick=948'),
                          frame(3, 33, host=1366).replace('frameTick=8', 'frameTick=1365')])
        ticks = frames.inspect(text)['tick_distance']
        self.assertEqual((ticks['samples'], ticks['min'], ticks['max']), (3, 1, 417))
        self.assertEqual(ticks['max_sample'], dict(line=2, seq=2, render=2,
                                                 hostTick=1365, frameTick=948))

    def test_tick_distance_is_signed_and_independent_of_hash_and_mode(self):
        text = '\n'.join([frame(1, 11, valid=0, host=7),
                          '[gs-present] host mode: RAM frames',
                          frame(1, 22, host=2).replace('frameTick=8', 'frameTick=3')])
        result = frames.inspect(text)
        ticks = result['tick_distance']
        self.assertEqual((ticks['samples'], ticks['min'], ticks['max']), (2, -1, -1))
        self.assertEqual(ticks['max_sample']['line'], 1)
        self.assertEqual(result['sequence_regressions'], [])

    def test_tick_distance_includes_retention_but_excludes_conflicting_metadata(self):
        text = '\n'.join([frame(1, 11), frame(1, 11, host=12),
                          frame(1, 22, host=1000), frame(2, 33, host=11)])
        result = frames.inspect(text)
        ticks = result['tick_distance']
        self.assertEqual(result['inconsistent_repeats'], [3])
        self.assertEqual((ticks['samples'], ticks['min'], ticks['max']), (3, 2, 4))
        self.assertEqual(ticks['max_sample']['line'], 2)

    def test_source_units_are_unknown_for_legacy_logs_and_explicit_for_new_ones(self):
        text = '\n'.join([frame(1, 11), frame(2, 22, preferred=0),
                          frame(3, 33, preferred=1, host=12)])
        result = frames.inspect(text)
        self.assertEqual(result['source_units'], dict(unknown=1, pages_8192=1, blocks_256=1))
        self.assertEqual(result['tick_distance']['max_sample']['preferred'], 1)

    def test_optional_source_selection_keeps_known_metadata_across_legacy_rows(self):
        text = '\n'.join([frame(1, 11), frame(1, 11, preferred=0),
                          frame(1, 11), frame(1, 11, preferred=1, host=1000)])
        result = frames.inspect(text)
        self.assertEqual(result['inconsistent_repeats'], [4])
        self.assertEqual(result['source_units'], dict(unknown=2, pages_8192=1, blocks_256=0))
        self.assertEqual(result['tick_distance']['max'], 2)

    def test_content_return_preserves_source_selection_without_assuming_corruption(self):
        text = '\n'.join([frame(1, 11, preferred=1), frame(2, 22, preferred=0),
                          frame(3, 11, preferred=1)])
        result = frames.inspect(text)
        self.assertEqual(result['content_returns'], [
            dict(line=3, seq=3, prior_seq=1, sourceFbp=208, frameTick=8,
                 frame_ticks=[8, 8, 8], preferred=1)])
        self.assertEqual(result['inconsistent_repeats'], [])

    def test_invalid_optional_flags_and_unknown_fields_are_rejected(self):
        for text in (frame(1, 11, preferred=2), frame(1, 11, preferred=-1),
                     frame(1, 11)+' extra=0', frame(1, 11)+' preferred=0 preferred=1'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                frames.inspect(text)

    def test_dimension_changes_keep_evidence_even_without_hash(self):
        result = frames.inspect('\n'.join([frame(1, 11), frame(2, 22, width=512, valid=0),
                                          frame(3, 33, width=512), frame(4, 44)]))
        self.assertEqual(result['dimension_changes'], [
            dict(line=2, seq=2, render=2, from_size=[640, 448], to_size=[512, 448]),
            dict(line=4, seq=4, render=4, from_size=[512, 448], to_size=[640, 448])])
        self.assertEqual(result['unknown_hashes'], 1)

    def test_conflicting_dimensions_and_separate_modes_do_not_create_false_changes(self):
        result = frames.inspect('\n'.join([frame(1, 11), frame(1, 11, width=512), frame(2, 22),
            '[gs-present] host mode: RAM frames', frame(1, 33, width=512), frame(2, 44, width=480)]))
        self.assertEqual(result['inconsistent_repeats'], [2])
        self.assertEqual(result['dimension_changes'], [
            dict(line=6, seq=2, render=2, from_size=[512, 448], to_size=[480, 448])])

    def test_content_return_classifies_frame_tick_parity_without_filtering_returns(self):
        for ticks, classification in [([21, 22, 23], 'alternating'),
                                      ([22, 24, 26], 'same'), ([21, 22, 24], 'other')]:
            with self.subTest(ticks=ticks):
                text = '\n'.join(frame(seq, value, host=100).replace('frameTick=8', f'frameTick={tick}')
                                 for seq, value, tick in zip((1, 2, 3), (11, 22, 11), ticks))
                result = frames.inspect(text)
                self.assertEqual(len(result['content_returns']), 1)
                self.assertEqual(result['content_returns'][0]['frame_ticks'], ticks)
                expected = dict(alternating=0, same=0, other=0)
                expected[classification] = 1
                self.assertEqual(result['content_return_parity'], expected)
                self.assertIn('no acredita SMODE2', result['limitation'])

    def test_content_return_parity_uses_acquired_ticks_and_ignores_retention(self):
        text = '\n'.join([frame(1, 11, host=100).replace('frameTick=8', 'frameTick=21'),
                          frame(1, 11, host=101).replace('frameTick=8', 'frameTick=21'),
                          frame(2, 22, host=104).replace('frameTick=8', 'frameTick=22'),
                          frame(3, 11, host=106).replace('frameTick=8', 'frameTick=23')])
        result = frames.inspect(text)
        self.assertEqual(result['retained'], 1)
        self.assertEqual(result['content_return_parity'], dict(alternating=1, same=0, other=0))
        self.assertEqual(result['content_returns'][0]['frame_ticks'], [21, 22, 23])

    def test_unknown_hash_or_mode_change_breaks_parity_evidence(self):
        for middle in (frame(2, 22, valid=0), '[gs-present] host mode: RAM frames\n'+frame(2, 22)):
            result = frames.inspect('\n'.join([frame(1, 11), middle, frame(3, 11)]))
            self.assertEqual(result['content_returns'], [])
            self.assertEqual(result['content_return_parity'], dict(alternating=0, same=0, other=0))

if __name__ == '__main__':
    unittest.main()
