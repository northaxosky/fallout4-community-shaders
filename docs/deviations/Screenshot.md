# Screenshot

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Own hotkey; suppresses the game's capture by clearing `bAllowScreenShot` | Print Screen and DevBench queue the engine's screenshot; the feature saves that request and `DisableVanillaScreenshot` skips the engine's file for it | The engine consumes one queued flag per frame, so the hotkey needs no second binding |
| Forced | Reads the back buffer after HDR conversion | While HDR Display splits the frame, scene and interface are composed at the engine's screenshot step (PQ BT.2020 for HDR PNG, gamma 2.2 otherwise) and the engine's own copy reads that composite | The frame buffer holds only the interface while the frame is split |
| Divergence | SDR captures are the pre-interface scene | The interface is included; `CaptureWithoutInterface` saves the scene alone | FO4's own screenshot includes the interface |
| Divergence | Free-form crop editor with saved presets and a live preview | Centered aspect presets (16:9, 21:9, 32:9, 4:3, 1:1), no preview | The forwarding UI has no draggable image editor |
| Divergence | HDR PNG from R10 or fp16 back buffers | R10 PQ only | The compose pass writes R10 |
| Pending | HDR AVIF (`HdrUseAvif`) | PNG only | The AVIF encoder is not ported |
| N/A | SDR BMP (`SdrUsePng`) | SDR is always PNG | Lossless PNG covers it |
