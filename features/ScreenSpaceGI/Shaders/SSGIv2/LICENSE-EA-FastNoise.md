# EA FastNoise / `fast_2uges.dds`

`fast_2uges.dds` is the 128x128x64 noise texture from Electronic Arts'
FastNoise project, redistributed verbatim under the MIT license.

- Source: <https://github.com/electronicarts/fastnoise>
- License: MIT (see below)
- Acquired via: upstream Skyrim Community Shaders @ commit `bb6460db`
  (`features/Screen Space GI/Shaders/ScreenSpaceGI/fast_2uges.dds`).

The asset is consumed by the ported XeGTAO + Visibility Bitmask compute
shaders in this directory; see `Common.hlsli` (`SpatioTemporalNoise()`)
for the sampling pattern.

## MIT License

```
Copyright (C) Electronic Arts Inc.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
