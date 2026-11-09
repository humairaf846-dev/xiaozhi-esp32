# Play a P3-format audio file
import opuslib
import struct
import numpy as np
import sounddevice as sd
import argparse

def play_p3_file(input_file):
    """
    Play a P3-format audio file
    P3 format: [1-byte type, 1-byte reserved, 2-byte length, Opus data]
    """
    # Initialize the Opus decoder
    sample_rate = 16000  # Sample rate is fixed at 16000Hz
    channels = 1  # Mono
    decoder = opuslib.Decoder(sample_rate, channels)

    # Frame size (60ms)
    frame_size = int(sample_rate * 60 / 1000)

    # Open the audio stream
    stream = sd.OutputStream(
        samplerate=sample_rate,
        channels=channels,
        dtype='int16'
    )
    stream.start()

    try:
        with open(input_file, 'rb') as f:
            print(f"Playing: {input_file}")

            while True:
                # Read the header (4 bytes)
                header = f.read(4)
                if not header or len(header) < 4:
                    break

                # Parse the header
                packet_type, reserved, data_len = struct.unpack('>BBH', header)

                # Read the Opus data
                opus_data = f.read(data_len)
                if not opus_data or len(opus_data) < data_len:
                    break

                # Decode the Opus data
                pcm_data = decoder.decode(opus_data, frame_size)

                # Convert the bytes to a numpy array
                audio_array = np.frombuffer(pcm_data, dtype=np.int16)

                # Play the audio
                stream.write(audio_array)

    except KeyboardInterrupt:
        print("\nPlayback stopped")
    finally:
        stream.stop()
        stream.close()
        print("Playback complete")

def main():
    parser = argparse.ArgumentParser(description='Play a P3-format audio file')
    parser.add_argument('input_file', help='Path to the input P3 file')
    args = parser.parse_args()

    play_p3_file(args.input_file)

if __name__ == "__main__":
    main()
