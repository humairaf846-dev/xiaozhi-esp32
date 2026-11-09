# LVGL Image Conversion Tools

This directory contains two Python scripts for processing and converting images into LVGL format:

## 1. LVGLImage (LVGLImage.py)

Taken from LVGL's [official repo](https://github.com/lvgl/lvgl), the conversion script [LVGLImage.py](https://github.com/lvgl/lvgl/blob/master/scripts/LVGLImage.py)

## 2. LVGL Image Conversion Tool (lvgl_tools_gui.py)

Calls `LVGLImage.py` to batch-convert images into LVGL image format.
Can be used to change XiaoZhi's default expressions; a step-by-step tutorial is [here](https://www.bilibili.com/video/BV12FQkYeEJ3/).

### Features

- A graphical interface for easier operation
- Batch image conversion
- Automatically detects the image format and picks the best color-format conversion
- Multi-resolution support

### Usage

Create a virtual environment
```bash
# Create venv
python -m venv venv
# Activate the environment
source venv/bin/activate  # Linux/Mac
venv\Scripts\activate      # Windows
```

Install dependencies
```bash
pip install -r requirements.txt
```

Run the conversion tool

```bash
# Activate the environment
source venv/bin/activate  # Linux/Mac
venv\Scripts\activate      # Windows
# Run
python lvgl_tools_gui.py
```
