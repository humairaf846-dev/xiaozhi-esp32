# AudioCodec Input Channel Audit

Audit date: 2026-09-29. Pre-change source baseline: `cf2024f`. The per-board list keeps the pre-change audit results; this round's adjustments are in the section below.

## This Round's Adjustments

Applied the single-mic-by-default policy to the following paths, while keeping AFE's support for multi-mic input:

| Board | Before | New default | Developer experimentation interface |
|---|---|---|---|
| Box Lite | MMR / MM | MR / M | `BoxAudioCodecLite` gained a new trailing `microphone_channels=1` parameter; pass `2` explicitly to test MMR / MM, reference switch logic unchanged. |
| ESP32-S31-Korvo-1 | MM | M | Board-level `AUDIO_INPUT_CHANNELS` now defaults to `1`; set it to `2` to still test MM. There is no reference, so MMR should not be declared. |
| T-CameraPlus-S3 V1.0/V1.1 | Mono capture, yet declared MR | M | Turned off the incorrect reference declaration to match the RX data. V1.2 was already M. |

All other M / MR paths are unchanged. The two LILYGO boards whose reference source still needs verification still require hardware confirmation. This change does not substitute for on-device verification, nor does it mean Box Lite's root cause has been confirmed.

See [Audio Design](../main/audio/README.md#board-input-layouts) for the developer interface and verification requirements.

## Pre-Change Audit: Scope and Conclusion

Reviewed `main/audio/`, every board-level codec, `GetAudioCodec()` constructor arguments, custom subclasses, and every `input_channels_` / `input_reference_` assignment. Covers 150 board directories with a `config.json`, plus the two source directories `kevin/sp-v3-dev` and `lilygo/t-display-p4`, for 152 directories total. A directory may have multiple configurations or hardware revisions; this table does not treat the directory count as the number of shipped firmware builds.

The format describes the data semantics a codec declares to the audio engine; resource-constrained chips may use LiteAudioEngine, which does not actually create an AFE. The number of I2S slots, the number of physical ADC inputs, and the number of mics seen by the upper layer are not the same thing. An `MR` declaration also does not prove that the reference input's wiring, waveform, and timing have been verified on real hardware.

- `M`: one microphone, no reference. Wake word and conversation work normally; does not provide the reference needed for on-device AEC.
- `MR`: one microphone and one playback reference.
- `MM`: two microphones, no reference.
- `MMR`: two microphones and one playback reference.

**Not every board should be switched to MR. The single-mic policy to adopt is: MR when a valid reference exists, M when it does not.** Most existing implementations already follow this channel-count policy; only Box Lite and ESP32-S31-Korvo-1 explicitly configure dual-mic input.

## Pre-Change Audit: Dual-Mic Configurations

| Board | Implementation | Current format | If standardizing on single mic |
|---|---|---|---|
| ESP32-S3-Box-Lite | BoxAudioCodecLite | MMR when audio processing is on, otherwise MM | `input_channels_ = input_reference_ ? 2 : 1`; EnableInput/Read would then be adjusted accordingly for single-mic capture and layout. Needs on-device verification of single-channel capture, the software reference, and calls. |
| ESP32-S31-Korvo-1 | Es8389AudioCodec | MM | Currently `AUDIO_INPUT_CHANNELS=2` with `input_reference_=false`; the single-mic policy should be M - the second mic channel must not be labeled R. Only this board's input configuration should change; the shared ES8389 class or its stereo output configuration should not be touched. |

Code references: [Box Lite](../main/boards/espressif/esp32-s3-box-lite/box_audio_codec_lite.cc), [S31 Korvo config](../main/boards/espressif/esp32-s31-korvo-1/config.h), [ES8389](../main/audio/codecs/es8389_audio_codec.cc). The current ALIENTEK Box2 WiFi / 4G both use the ES8389 constructor's default single-mic input, not dual-mic.

## Pre-Change Audit: Reference-Channel Exceptions

### LILYGO T-CameraPlus-S3 V1.0/V1.1: a clear format mismatch

`config.h` sets `AUDIO_INPUT_REFERENCE=true`, so the codec declares two-channel MR, but `CreateVoiceHardware()` configures the microphone RX as `I2S_SLOT_MODE_MONO`. `Read()` reads mono samples directly with no playback reference inserted; `Write()` does not set up a software loopback either. As a result the upper layer interprets consecutive mic samples as M/R pairs, and the read duration does not match the upper layer's frame length either.

This path should first be reconciled against the actual mono input and changed to M, rather than keep being unified to MR. V1.2 uses mono PDM and declares M, so it is unaffected by this mismatch. This is an independent source-level finding and should not be taken as the root cause of the Box Lite issue.

Reference: [config](../main/boards/lilygo/t-cameraplus-s3/config.h), [codec](../main/boards/lilygo/t-cameraplus-s3/tcamerapluss3_audio_codec.cc).

### LILYGO T-Circle-S3 / T-Display-S3-Pro-MVSrLora: reference source still needs hardware verification

Both declare MR and read directly from a stereo mic I2S RX, with no software-inserted playback reference. The source confirms the two-channel layout but cannot prove that the second slot is wired to a playback loopback; the schematic or a recording needs to be checked. `input_reference=true` alone should not be used to conclude the AEC reference is valid, and slot/pin should not be changed directly without verifying the hardware first.

Reference: [T-Circle](../main/boards/lilygo/t-circle-s3/tcircles3_audio_codec.cc), [T-Display](../main/boards/lilygo/t-display-s3-pro-mvsrlora/tdisplays3promvsrlora_audio_codec.cc). In the per-board table, † marks the mismatch or pending-verification reference source described above.

### Reference macro defined, but the actual codec is still M

For example, directories such as M5Stack Atom EchoS3R and AtomS3R Echo Base define `AUDIO_INPUT_REFERENCE=true`, but instantiate `Es8311AudioCodec`, which does not accept that parameter. That class fixes `input_reference_=false` and `input_channels_=1`. Changing the macro does not automatically produce a loopback. The audit goes by the actual constructor arguments and codec implementation.

### Software reference and hardware reference verified separately

Box Lite and AIVOX3 construct the reference with a software buffer between Read/Write; implementations such as BoxAudioCodec read the reference from a selected ADC/I2S channel. Unifying everything to MR only unifies the layout - it does not unify the timing or echo-cancellation behavior of the two approaches.

Box Lite's shared reference buffer and its read/write positions have no concurrency protection, and a capture error still returns success - that is a separate issue needing its own verification and fix; switching to single mic is not a substitute for these checks.

## Follow-Up Hardware Verification

1. Box Lite: compare the default single-mic MR/M against the explicit dual-mic MMR/MM, verifying wake word, calls, re-listening after playback, interruption and reconnection, and on-device AEC on/off. The single-mic configuration is not a substitute for checking the software reference timing and concurrency.
2. T-CameraPlus-S3: verify V1.0/V1.1's mono capture and M declaration, and the V1.2 regression.
3. S31 Korvo-1: verify default M capture and stereo playback; there is currently no reproduction evidence for this board's original fault, so it should not be claimed as fixed.
4. Check the reference source for both LILYGO boards. For boards without a reference, adding MR requires actually providing and aligning the reference data - not just changing the channel count or format string.

The earlier analysis of ESP-SR's internal output path is still an unverified hypothesis and should not be taken as evidence that the fault has been fixed. No on-device verification was done this round.

## This Round's Verification Record

- Host tests: `python3 -m unittest discover -s scripts/tests -v`, 81 passed.
- Format check: the 4 modified C/C++ files pass the repository's configured clang-format check; the diff whitespace check preserves Box Lite's original CRLF line endings.
- Box Lite / T-CameraPlus-S3 V1.0/V1.1: called `scripts/build.py` per the standard procedure; ESP-IDF 6.1 was blocked during the dependency configuration stage, reporting that `ESP_VIDEO_USE_CUSTOMIZED_ESP_H264_VERSION` does not exist. Retrying Box Lite with the supported 6.0.2 was also blocked by the configuration check (`Missing required kconfig option after retry`). Firmware builds for these two were not completed.
- ESP32-S31-Korvo-1: ESP-IDF `v6.1-beta1-701-g812f3e98ca5`, the standard build and firmware merge passed. The current local build directory corresponds to this board.
- No physical hardware testing was performed; the dual-mic experimental path has not yet been verified on real hardware.

## Pre-Change Per-Board List

Several codecs represent a compile-time option, hardware revision, or runtime choice. The format is annotated per entry so that different paths within the same directory are not merged into a single conclusion.

| Board directory | codec → declared format |
|---|---|
| [alientek/atk-dnesp32s3](../main/boards/alientek/atk-dnesp32s3/) | `Es8388AudioCodec` → M |
| [alientek/atk-dnesp32s3-box](../main/boards/alientek/atk-dnesp32s3-box/) | `ATK_NoAudioCodecDuplex` → M<br>`Es8311AudioCodec` → M |
| [alientek/atk-dnesp32s3-box0](../main/boards/alientek/atk-dnesp32s3-box0/) | `Es8311AudioCodec` → M |
| [alientek/atk-dnesp32s3-box2-4g](../main/boards/alientek/atk-dnesp32s3-box2-4g/) | `Es8389AudioCodec` → M |
| [alientek/atk-dnesp32s3-box2-wifi](../main/boards/alientek/atk-dnesp32s3-box2-wifi/) | `Es8389AudioCodec` → M |
| [alientek/atk-dnesp32s3-box3](../main/boards/alientek/atk-dnesp32s3-box3/) | `BoxAudioCodec` → MR |
| [alientek/atk-dnesp32s3m-4g](../main/boards/alientek/atk-dnesp32s3m-4g/) | `Es8388AudioCodec` → M |
| [alientek/atk-dnesp32s3m-wifi](../main/boards/alientek/atk-dnesp32s3m-wifi/) | `Es8388AudioCodec` → M |
| [bread-compact-esp32](../main/boards/bread-compact-esp32/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [bread-compact-esp32-lcd](../main/boards/bread-compact-esp32-lcd/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [bread-compact-ml307](../main/boards/bread-compact-ml307/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [bread-compact-nt26](../main/boards/bread-compact-nt26/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [bread-compact-wifi](../main/boards/bread-compact-wifi/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [bread-compact-wifi-lcd](../main/boards/bread-compact-wifi-lcd/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [bread-compact-wifi-s3cam](../main/boards/bread-compact-wifi-s3cam/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [dfrobot/df-k10](../main/boards/dfrobot/df-k10/) | `K10AudioCodec` → MR |
| [dfrobot/df-s3-ai-cam](../main/boards/dfrobot/df-s3-ai-cam/) | `NoAudioCodecSimplexPdm` → M |
| [doit-s3-aibox](../main/boards/doit-s3-aibox/) | `NoAudioCodecSimplexPdm` → M |
| [du-chatx](../main/boards/du-chatx/) | `NoAudioCodecSimplex` → M |
| [electron-bot](../main/boards/electron-bot/) | `NoAudioCodecSimplex` → M |
| [espressif/esp-hi](../main/boards/espressif/esp-hi/) | `AdcPdmAudioCodec` → M |
| [espressif/esp-sensairshuttle](../main/boards/espressif/esp-sensairshuttle/) | `AdcPdmAudioCodec` → M |
| [espressif/esp-sparkbot](../main/boards/espressif/esp-sparkbot/) | `SparkBotEs8311AudioCodec` → M |
| [espressif/esp-spot](../main/boards/espressif/esp-spot/) | `Es8311AudioCodec` → M |
| [espressif/esp-vocat](../main/boards/espressif/esp-vocat/) | `BoxAudioCodec` → MR |
| [espressif/esp32-p4-function-ev-board](../main/boards/espressif/esp32-p4-function-ev-board/) | `Es8311AudioCodec` → M |
| [espressif/esp32-s3-box](../main/boards/espressif/esp32-s3-box/) | `BoxAudioCodec` → MR |
| [espressif/esp32-s3-box-3](../main/boards/espressif/esp32-s3-box-3/) | `BoxAudioCodec` → MR |
| [espressif/esp32-s3-box-lite](../main/boards/espressif/esp32-s3-box-lite/) | `BoxAudioCodecLite` → MMR / MM (depending on the audio-processing switch) |
| [espressif/esp32-s3-korvo-2-v3.0](../main/boards/espressif/esp32-s3-korvo-2-v3.0/) | `BoxAudioCodec` → MR |
| [espressif/esp32-s3-korvo-2-v3.0-rndis](../main/boards/espressif/esp32-s3-korvo-2-v3.0-rndis/) | `BoxAudioCodec` → MR |
| [espressif/esp32-s3-lcd-ev-board](../main/boards/espressif/esp32-s3-lcd-ev-board/) | `BoxAudioCodec` → MR |
| [espressif/esp32-s3-lcd-ev-board-2](../main/boards/espressif/esp32-s3-lcd-ev-board-2/) | `BoxAudioCodec` → MR |
| [espressif/esp32-s31-function-coreboard-1](../main/boards/espressif/esp32-s31-function-coreboard-1/) | `Es8311AudioCodec` → M |
| [espressif/esp32-s31-korvo-1](../main/boards/espressif/esp32-s31-korvo-1/) | `Es8389AudioCodec` → MM |
| [folotoy/ai-passport](../main/boards/folotoy/ai-passport/) | `Es8311AudioCodec` → M |
| [freenove-esp32s3-display-2.8-lcd](../main/boards/freenove-esp32s3-display-2.8-lcd/) | `Es8311AudioCodec` → M |
| [genjutech-s3-1.54tft](../main/boards/genjutech-s3-1.54tft/) | `SparkBotEs8311AudioCodec` → M |
| [hu-087](../main/boards/hu-087/) | `NoAudioCodecSimplex` → M |
| [jiuchuan-s3](../main/boards/jiuchuan-s3/) | `Es8311AudioCodec` → M |
| [kevin/box-2](../main/boards/kevin/box-2/) | `BoxAudioCodec` → MR |
| [kevin/c3](../main/boards/kevin/c3/) | `Es8311AudioCodec` → M |
| [kevin/sp-v3-dev](../main/boards/kevin/sp-v3-dev/) | `NoAudioCodecSimplex` → M |
| [kevin/sp-v4-dev](../main/boards/kevin/sp-v4-dev/) | `Es8311AudioCodec` → M |
| [kevin/yuying-313lcd](../main/boards/kevin/yuying-313lcd/) | `Es8311AudioCodec` → M |
| [labplus/ledong-v2](../main/boards/labplus/ledong-v2/) | `Es8388AudioCodec` → M |
| [labplus/mpython-v3](../main/boards/labplus/mpython-v3/) | `Es8388AudioCodec` → M |
| [lcdwiki-es3c35p](../main/boards/lcdwiki-es3c35p/) | `Es8311AudioCodec` → M |
| [lceda-course-examples/eda-robot-pro](../main/boards/lceda-course-examples/eda-robot-pro/) | `NoAudioCodecSimplex` → M |
| [lceda-course-examples/eda-super-bear](../main/boards/lceda-course-examples/eda-super-bear/) | `NoAudioCodecSimplex` → M |
| [lceda-course-examples/eda-tv-pro](../main/boards/lceda-course-examples/eda-tv-pro/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [lckfb/szpi-esp32c3](../main/boards/lckfb/szpi-esp32c3/) | `Es8311AudioCodec` → M |
| [lckfb/szpi-esp32s3](../main/boards/lckfb/szpi-esp32s3/) | `CustomAudioCodec` → MR |
| [lilygo/t-cameraplus-s3](../main/boards/lilygo/t-cameraplus-s3/) | `Tcamerapluss3AudioCodec` → MR† (V1.0/V1.1) / M (V1.2) |
| [lilygo/t-circle-s3](../main/boards/lilygo/t-circle-s3/) | `Tcircles3AudioCodec` → MR† |
| [lilygo/t-display-p4](../main/boards/lilygo/t-display-p4/) | `Es8311AudioCodec` → M |
| [lilygo/t-display-s3-pro-mvsrlora](../main/boards/lilygo/t-display-s3-pro-mvsrlora/) | `Tdisplays3promvsrloraAudioCodec` → MR† |
| [m5stack/atom-echos3r](../main/boards/m5stack/atom-echos3r/) | `Es8311AudioCodec` → M |
| [m5stack/atommatrix-echo-base](../main/boards/m5stack/atommatrix-echo-base/) | `Es8311AudioCodec` → M |
| [m5stack/atoms3-echo-base](../main/boards/m5stack/atoms3-echo-base/) | `Es8311AudioCodec` → M |
| [m5stack/atoms3r-cam-m12-echo-base](../main/boards/m5stack/atoms3r-cam-m12-echo-base/) | `Es8311AudioCodec` → M |
| [m5stack/atoms3r-echo-base](../main/boards/m5stack/atoms3r-echo-base/) | `Es8311AudioCodec` → M |
| [m5stack/atoms3r-echo-pyramid](../main/boards/m5stack/atoms3r-echo-pyramid/) | `PyramidAudioCodec` → MR |
| [m5stack/cardputer-adv](../main/boards/m5stack/cardputer-adv/) | `CardputerAdvEs8311` → M |
| [m5stack/core-s3](../main/boards/m5stack/core-s3/) | `CoreS3AudioCodec` → M |
| [m5stack/corep4](../main/boards/m5stack/corep4/) | `BoxAudioCodec` → MR |
| [m5stack/stick-s3](../main/boards/m5stack/stick-s3/) | `Sticks3AudioCodec` → M |
| [m5stack/stopwatch](../main/boards/m5stack/stopwatch/) | `Es8311AudioCodec` → M |
| [m5stack/tab5](../main/boards/m5stack/tab5/) | `Tab5AudioCodec` → MR |
| [magiclick/2p4](../main/boards/magiclick/2p4/) | `Es8311AudioCodec` → M |
| [magiclick/2p5](../main/boards/magiclick/2p5/) | `Es8311AudioCodec` → M |
| [magiclick/c3](../main/boards/magiclick/c3/) | `Es8311AudioCodec` → M |
| [magiclick/c3-v2](../main/boards/magiclick/c3-v2/) | `Es8311AudioCodec` → M |
| [maza-ai/esp32-s3](../main/boards/maza-ai/esp32-s3/) | `Es8311AudioCodec` → M |
| [maza-ai/esp32-s31](../main/boards/maza-ai/esp32-s31/) | `Es8311AudioCodec` → M |
| [minsi-k08-dual](../main/boards/minsi-k08-dual/) | `NoAudioCodecSimplex` → M |
| [mixgo-nova](../main/boards/mixgo-nova/) | `Es8374AudioCodec` → M |
| [movecall/cuican-esp32s3](../main/boards/movecall/cuican-esp32s3/) | `Es8311AudioCodec` → M |
| [movecall/moji-esp32s3](../main/boards/movecall/moji-esp32s3/) | `Es8311AudioCodec` → M |
| [movecall/moji2-esp32c5](../main/boards/movecall/moji2-esp32c5/) | `Es8311AudioCodec` → M |
| [nologo/xingzhi-abs-2.0](../main/boards/nologo/xingzhi-abs-2.0/) | `Es8311AudioCodec` → M |
| [nologo/xingzhi-cube-0.85tft-ml307](../main/boards/nologo/xingzhi-cube-0.85tft-ml307/) | `NoAudioCodecSimplex` → M |
| [nologo/xingzhi-cube-0.85tft-wifi](../main/boards/nologo/xingzhi-cube-0.85tft-wifi/) | `NoAudioCodecSimplex` → M |
| [nologo/xingzhi-cube-0.96oled-ml307](../main/boards/nologo/xingzhi-cube-0.96oled-ml307/) | `NoAudioCodecSimplex` → M |
| [nologo/xingzhi-cube-0.96oled-wifi](../main/boards/nologo/xingzhi-cube-0.96oled-wifi/) | `NoAudioCodecSimplex` → M |
| [nologo/xingzhi-cube-1.54tft-ml307](../main/boards/nologo/xingzhi-cube-1.54tft-ml307/) | `NoAudioCodecSimplex` → M |
| [nologo/xingzhi-cube-1.54tft-wifi](../main/boards/nologo/xingzhi-cube-1.54tft-wifi/) | `NoAudioCodecSimplex` → M |
| [nologo/xingzhi-metal-1.54-wifi](../main/boards/nologo/xingzhi-metal-1.54-wifi/) | `Es8311AudioCodec` → M |
| [nulllab-ai-vox-v3](../main/boards/nulllab-ai-vox-v3/) | `AIVOX3AudioCodec` → MR / M (depending on the device AEC switch) |
| [otto-robot](../main/boards/otto-robot/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [quandong-s3-dev](../main/boards/quandong-s3-dev/) | `Es8311AudioCodec` → M |
| [rymcu/bigsmart](../main/boards/rymcu/bigsmart/) | `CustomAudioCodec` → MR |
| [sensecap-watcher](../main/boards/sensecap-watcher/) | `SensecapAudioCodec` → M |
| [spotpear/sp-esp32-s3-1.28-box](../main/boards/spotpear/sp-esp32-s3-1.28-box/) | `Es8311AudioCodec` → M |
| [spotpear/sp-esp32-s3-1.54-muma](../main/boards/spotpear/sp-esp32-s3-1.54-muma/) | `Es8311AudioCodec` → M |
| [surfer-c3-1.14tft](../main/boards/surfer-c3-1.14tft/) | `Es8311AudioCodec` → M |
| [taiji-pi-s3](../main/boards/taiji-pi-s3/) | `NoAudioCodecSimplex` → M<br>`NoAudioCodecSimplexPdm` → M |
| [waveshare/esp32-c5-touch-lcd-1.69](../main/boards/waveshare/esp32-c5-touch-lcd-1.69/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-c6-epaper-1.54](../main/boards/waveshare/esp32-c6-epaper-1.54/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-c6-lcd-0.85](../main/boards/waveshare/esp32-c6-lcd-0.85/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-c6-lcd-1.69](../main/boards/waveshare/esp32-c6-lcd-1.69/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-c6-touch-amoled-1.32](../main/boards/waveshare/esp32-c6-touch-amoled-1.32/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-c6-touch-amoled-1.43](../main/boards/waveshare/esp32-c6-touch-amoled-1.43/) | `BoxAudioCodec` → M |
| [waveshare/esp32-c6-touch-amoled-1.8](../main/boards/waveshare/esp32-c6-touch-amoled-1.8/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-c6-touch-amoled-2.06](../main/boards/waveshare/esp32-c6-touch-amoled-2.06/) | `BoxAudioCodec` → M |
| [waveshare/esp32-c6-touch-amoled-2.16](../main/boards/waveshare/esp32-c6-touch-amoled-2.16/) | `BoxAudioCodec` → M |
| [waveshare/esp32-c6-touch-lcd-1.54](../main/boards/waveshare/esp32-c6-touch-lcd-1.54/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-c6-touch-lcd-1.83](../main/boards/waveshare/esp32-c6-touch-lcd-1.83/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-p4-nano](../main/boards/waveshare/esp32-p4-nano/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-p4-wifi6-touch-lcd](../main/boards/waveshare/esp32-p4-wifi6-touch-lcd/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-p4-wifi6-touch-lcd-3.5](../main/boards/waveshare/esp32-p4-wifi6-touch-lcd-3.5/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-s3-audio-board](../main/boards/waveshare/esp32-s3-audio-board/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-cam](../main/boards/waveshare/esp32-s3-cam/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-epaper-1.54](../main/boards/waveshare/esp32-s3-epaper-1.54/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-s3-epaper-3.97](../main/boards/waveshare/esp32-s3-epaper-3.97/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-s3-lcd-0.85](../main/boards/waveshare/esp32-s3-lcd-0.85/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-rgb-matrix](../main/boards/waveshare/esp32-s3-rgb-matrix/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-rlcd-4.2](../main/boards/waveshare/esp32-s3-rlcd-4.2/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-amoled-1.32](../main/boards/waveshare/esp32-s3-touch-amoled-1.32/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-s3-touch-amoled-1.43c](../main/boards/waveshare/esp32-s3-touch-amoled-1.43c/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-amoled-1.75](../main/boards/waveshare/esp32-s3-touch-amoled-1.75/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-amoled-1.8](../main/boards/waveshare/esp32-s3-touch-amoled-1.8/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-s3-touch-amoled-1.8-v2](../main/boards/waveshare/esp32-s3-touch-amoled-1.8-v2/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-s3-touch-amoled-2.06](../main/boards/waveshare/esp32-s3-touch-amoled-2.06/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-amoled-2.16](../main/boards/waveshare/esp32-s3-touch-amoled-2.16/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-lcd-1.46](../main/boards/waveshare/esp32-s3-touch-lcd-1.46/) | `NoAudioCodecSimplex` → M |
| [waveshare/esp32-s3-touch-lcd-1.54](../main/boards/waveshare/esp32-s3-touch-lcd-1.54/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-lcd-1.83](../main/boards/waveshare/esp32-s3-touch-lcd-1.83/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-lcd-1.85](../main/boards/waveshare/esp32-s3-touch-lcd-1.85/) | `NoAudioCodecSimplex` → M |
| [waveshare/esp32-s3-touch-lcd-1.85b](../main/boards/waveshare/esp32-s3-touch-lcd-1.85b/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-lcd-1.85c](../main/boards/waveshare/esp32-s3-touch-lcd-1.85c/) | `BoxAudioCodec` → MR<br>`NoAudioCodecSimplex` → M |
| [waveshare/esp32-s3-touch-lcd-3.49](../main/boards/waveshare/esp32-s3-touch-lcd-3.49/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-lcd-3.49-v2](../main/boards/waveshare/esp32-s3-touch-lcd-3.49-v2/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-lcd-3.5](../main/boards/waveshare/esp32-s3-touch-lcd-3.5/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-s3-touch-lcd-3.5b](../main/boards/waveshare/esp32-s3-touch-lcd-3.5b/) | `Es8311AudioCodec` → M |
| [waveshare/esp32-s3-touch-lcd-4.3c](../main/boards/waveshare/esp32-s3-touch-lcd-4.3c/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-lcd-4b](../main/boards/waveshare/esp32-s3-touch-lcd-4b/) | `BoxAudioCodec` → MR |
| [waveshare/esp32-s3-touch-lcd-7c](../main/boards/waveshare/esp32-s3-touch-lcd-7c/) | `WaveshareAudioCodec` → MR |
| [waveshare/esp32-touch-lcd-3.5](../main/boards/waveshare/esp32-touch-lcd-3.5/) | `Es8311AudioCodec` → M |
| [wdmomo/esp32-cgc](../main/boards/wdmomo/esp32-cgc/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [wdmomo/esp32-cgc-144](../main/boards/wdmomo/esp32-cgc-144/) | `NoAudioCodecSimplex` → M |
| [wk-esp32s3-dev](../main/boards/wk-esp32s3-dev/) | `NoAudioCodecDuplex` → M<br>`NoAudioCodecSimplex` → M |
| [xmini/c3](../main/boards/xmini/c3/) | `Es8311AudioCodec` → M |
| [xmini/c3-4g](../main/boards/xmini/c3-4g/) | `Es8311AudioCodec` → M |
| [xmini/c3-v3](../main/boards/xmini/c3-v3/) | `Es8311AudioCodec` → M |
| [xorigin/aipi-lite](../main/boards/xorigin/aipi-lite/) | `Es8311AudioCodec` → M |
| [yunliao-s3](../main/boards/yunliao-s3/) | `Es8388AudioCodec` → MR |
| [zectrix/zectrix-s3-epaper-4.2](../main/boards/zectrix/zectrix-s3-epaper-4.2/) | `Es8311AudioCodec` → M |
| [zhengchen/1.54tft-ml307](../main/boards/zhengchen/1.54tft-ml307/) | `NoAudioCodecSimplex` → M |
| [zhengchen/1.54tft-wifi](../main/boards/zhengchen/1.54tft-wifi/) | `NoAudioCodecSimplex` → M |
| [zhengchen/cam](../main/boards/zhengchen/cam/) | `CustomAudioCodec` → MR |
| [zhengchen/cam-ml307](../main/boards/zhengchen/cam-ml307/) | `CustomAudioCodec` → MR |
