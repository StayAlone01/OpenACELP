#!/bin/bash 

#-----------------------------------------------------------
# This script converts NIST SPHERE .sph files to .wav
# It normalizes the amplitude to -1dB, applies a lowpass
# at 3400Hz and resamples the output to 8000Hz
#
# This can is used to produce a training set
# for LSF vector quantization using TED-LIUM speech corpus.
#
# SP5WWP 07/04/2020
#-----------------------------------------------------------

# Retain generated WAV files?
retain=$1

echo "Processing..."

# Convert all .sph files to .wav
# Do it if the .wav file doesn't exist already
# Trim out the crap (20s on both ends)
# Of course some windowing should be added
# To avoid sudden signal changes,
# But we can live with that
for f in *.sph
do
	if [ ! -f "${f%.*}.wav" ]
	then
		sox -t sph "$f" -b 16 -t wav "${f%.*}.wav" trim 20 -20
	fi
done

# Normalize to -1dB
for f in *.wav
do
	sox --norm=-1 "$f" "${f%.*}_new.wav"
	rm "$f"
	mv "${f%.*}_new.wav" "$f"
done

# Lowpass at 3.4kHz, 4 passes
# Maybe sox would be better here
for f in *.wav
do
	for passes in {1..4}
	do
		ffmpeg -hide_banner -loglevel panic -i "$f" -filter lowpass=3400 "${f%.*}_new.wav"
		rm "$f"
		mv "${f%.*}_new.wav" "$f"
	done
done

# Resample to 8000Hz
for f in *.wav
do
	echo "$f"
	sox "$f" -r 8000 "${f%.*}_new.wav"
	rm "$f"
	mv "${f%.*}_new.wav" "$f"
done

# Merge all pieces into one and add noise
# It is needed, so the autocorrelator
# Won't go crazy at longer silence periods
echo "Post processing..."
sox $(ls *.wav) corpus.wav
sox corpus.wav -p synth whitenoise vol 0.001 | sox -m corpus.wav - corpus.raw

# Delete old WAVs
if [ $retain == 0 ]
then
	rm *.wav
fi

echo "Done."
