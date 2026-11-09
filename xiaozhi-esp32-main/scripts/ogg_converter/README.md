# ogg_converter - XiaoZhi AI OGG Sound-Effect Batch Converter

This script is an OGG batch-conversion tool; it converts input audio files into the OGG format XiaoZhi can use.

Built on the third-party Python library `ffmpeg-python`, which **requires** an `ffmpeg` environment.

You can download the ffmpeg release for your own system [here](https://ffmpeg.org/download.html), then add it to your PATH or place it in the script's directory.

Supports converting between OGG and other audio formats, loudness adjustment, and more.

# Create and activate a virtual environment

```bash
# Create the virtual environment
python -m venv venv
# Activate the virtual environment
source venv/bin/activate # Mac/Linux
venv\Scripts\activate # Windows
```
# Download FFmpeg
Download ffmpeg [here](https://ffmpeg.org/download.html).

Download the build matching your current system, then place the `ffmpeg` executable in the script's directory, or add the directory containing it to your PATH.

# Install dependencies
Run this inside the virtual environment

```bash
pip install ffmpeg-python
```

# Run the script
```bash
python ogg_covertor.py
```
