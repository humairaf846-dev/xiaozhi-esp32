# SPIFFS Assets Builder

This script builds the SPIFFS assets partition for an ESP32 project, packaging various resource files into a format usable on the device.

## Features

- Processes the wake-word network model (WakeNet Model)
- Integrates text font files
- Processes emoji image collections
- Automatically generates the resource index file
- Packs everything into the final `assets.bin` file

## Requirements

- Python 3.6+
- The relevant resource files

## Usage

### Basic syntax

```bash
./build.py --wakenet_model <wakenet_model_dir> \
    --text_font <text_font_file> \
    --emoji_collection <emoji_collection_dir>
```

### Arguments

| Argument | Type | Required | Description |
|------|------|------|------|
| `--wakenet_model` | directory path | No | Path to the wake-word network model directory |
| `--text_font` | file path | No | Path to the text font file |
| `--emoji_collection` | directory path | No | Path to the emoji image collection directory |

### Examples

```bash
# Full example with all arguments
./build.py \
    --wakenet_model ../../managed_components/espressif__esp-sr/model/wakenet_model/wn9_nihaoxiaozhi_tts \
    --text_font ../../components/noto-fonts/cbin/font_noto_sans_common_20_4.bin \
    --emoji_collection ../../components/noto-fonts/png/noto-color-emoji_64/

# Process only the font file
./build.py --text_font ../../components/noto-fonts/cbin/font_noto_sans_common_20_4.bin

# Process only the emoji collection
./build.py --emoji_collection ../../components/noto-fonts/png/noto-color-emoji_64/
```

## Workflow

1. **Create the build directory structure**
   - `build/` - main build directory
   - `build/assets/` - resource files directory
   - `build/output/` - output files directory

2. **Process the wake-word network model**
   - Copy the model files into the build directory
   - Use `pack_model.py` to generate `srmodels.bin`
   - Copy the generated model file into the assets directory

3. **Process the text font**
   - Copy the font file into the assets directory
   - Supports `.bin`-format font files

4. **Process the emoji collection**
   - Scan the given directory for image files
   - Supports `.png` and `.gif` formats
   - Automatically generates the emoji index

5. **Generate configuration files**
   - `index.json` - the resource index file
   - `config.json` - the build configuration file

6. **Pack the final assets**
   - Use `spiffs_assets_gen.py` to generate `assets.bin`
   - Copy it to the build root

## Output Files

Once the build finishes, the following files are generated under `build/`:

- `assets/` - all resource files
- `assets.bin` - the final SPIFFS assets file
- `config.json` - the build configuration
- `output/` - intermediate output files

## Supported Resource Formats

- **Model files**: `.bin` (processed via pack_model.py)
- **Font files**: `.bin`
- **Image files**: `.png`, `.gif`
- **Config files**: `.json`

## Error Handling

The script includes thorough error handling:

- Checks whether source files/directories exist
- Verifies the result of subprocess calls
- Provides detailed error messages and warnings

## Notes

1. Make sure all the Python scripts it depends on are in the same directory
2. Resource file paths can be absolute, or relative to the script's directory
3. The build process cleans up previous build files
4. The generated `assets.bin` file's size is limited by the SPIFFS partition size
