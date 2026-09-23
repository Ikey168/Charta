# Three-room demo sample

`doodlebound-three-room.svg` is the source printable field. The 1024-pixel PNG is a raster
copy for the Android sample/import flow. Both files were created for this repository and
may be included in the Doodlebound demo. Print or display the square alone; surrounding
text or colored legends can be detected as extra walls and symbols.

The implemented basic legend is green=start, blue=exit, yellow=coin and red=enemy. A 256-pixel
render of the SVG was passed through `doodle::interpretPhoto`: it produced seven wall
polylines and exactly one of each intended symbol, with `readyToPlay()` true. The automated
`test_doodle_phone_sheet` regression constructs the same basic pattern at normal, dim and
bright levels without graphics or Android dependencies. These checks are synthetic; physical photo quality still requires
the [device acceptance plan](../../docs/ANDROID_DEMO_ACCEPTANCE.md).
