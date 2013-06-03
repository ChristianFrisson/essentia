/*
 * Copyright (C) 2006-2008 Music Technology Group (MTG)
 *                         Universitat Pompeu Fabra
 *
 */

#include "melbands.h"

using namespace std;
using namespace essentia;
using namespace standard;

const char* MelBands::name = "MelBands";
const char* MelBands::description = DOC("This algorithm computes the energy in mel bands for a given spectrum. It applies a frequency-domain filterbank (MFCC FB-40, [1]), which consists of equal area triangular filters spaced according to the mel scale. The filterbank is normalized in such a way that the sum of coefficients for every filter equals one. It is recommended that the input \"spectrum\" be calculated by the Spectrum algorithm.\n"
"\n"
"It is required that parameter \"highMelFrequencyBound\" not be larger than the Nyquist frequency, but must be larger than the parameter, \"lowMelFrequencyBound\". Also, The input spectrum must contain at least two elements. If any of these requirements are violated, an exception is thrown.\n"
"\n"
"References:\n"
"  [1] T. Ganchev, N. Fakotakis, G. Kokkinakisi, Comparative Evaluation of Various MFCC Implementations on the Speaker Verification Task\n"
"      Proceedings of the 10th International Conference on Speech and Computer, Patras, Greece, 2005\n"
"  [2] Mel-frequency cepstrum - Wikipedia, the free encyclopedia,\n"
"      http://en.wikipedia.org/wiki/Mel_frequency_cepstral_coefficient");

void MelBands::configure() {
  if (parameter("highFrequencyBound").toReal() > parameter("sampleRate").toReal()*0.5 ) {
    throw EssentiaException("MelBands: High frequency bound cannot be higher than Nyquist frequency");
  }
  if (parameter("highFrequencyBound").toReal() <= parameter("lowFrequencyBound").toReal()) {
    throw EssentiaException("MelBands: High frequency bound cannot be lower than the low frequency bound.");
  }

  _numBands = parameter("numberBands").toInt();
  _sampleRate = parameter("sampleRate").toReal();

  calculateFilterFrequencies();
  createFilters(parameter("inputSize").toInt());
}

void MelBands::calculateFilterFrequencies() {
  int filterSize = _numBands;

  _filterFrequencies.resize(filterSize + 2);

  // get the low and high frequency bounds in mel frequency
  Real lowMelFrequencyBound = hz2mel(parameter("lowFrequencyBound").toReal());
  Real highMelFrequencyBound = hz2mel(parameter("highFrequencyBound").toReal());
  Real melFrequencyIncrement = (highMelFrequencyBound - lowMelFrequencyBound)/(filterSize + 1);

  Real melFreq = lowMelFrequencyBound;
  for (int i=0; i<filterSize + 2; ++i) {
    _filterFrequencies[i] = mel2hz(melFreq);
    melFreq += melFrequencyIncrement; // increment linearly in mel-scale
  }
}

void MelBands::createFilters(int spectrumSize) {
  /*
  Calculate the filter coefficients...
  Basically what we're doing here is the following. Every filter is
  a triangle starting at frequency [i] and going to frequency [i+2].
  This way we have overlap for each filter with the next and the previous one.

        /\
  _____/  \_________
      i    i+2

  After that we normalize the filter over the whole range making sure it's
  norm is 1.

  We could use the optimized scheme from HTK/CLAM/Amadeus, but this makes
  it so much harder to understand what's going on. And you can't have more
  than half-band overlaps either (if needed).
  */

  if (spectrumSize < 2) {
    throw EssentiaException("MelBands: Filter bank cannot be computed from a spectrum with less than 2 bins");
  }

  int filterSize = parameter("numberBands").toInt();

  _filterCoefficients = vector<vector<Real> >(filterSize, vector<Real>(spectrumSize, 0.0));

  Real frequencyScale = (parameter("sampleRate").toReal() / 2.0) / (spectrumSize - 1);

  for (int i=0; i<filterSize; ++i) {
    Real fstep1 = hz2mel(_filterFrequencies[i+1]) - hz2mel(_filterFrequencies[i]);
    Real fstep2 = hz2mel(_filterFrequencies[i+2]) - hz2mel(_filterFrequencies[i+1]);

    int jbegin = int(_filterFrequencies[i] / frequencyScale + 0.5);
    int jend = int(_filterFrequencies[i+2] / frequencyScale + 0.5);

    for (int j=jbegin; j<jend; ++j) {
      Real binfreq = j*frequencyScale;
      // in the ascending part of the triangle...
      if ((binfreq >= _filterFrequencies[i]) && (binfreq < _filterFrequencies[i+1])) {
        _filterCoefficients[i][j] = (hz2mel(binfreq) - hz2mel(_filterFrequencies[i])) / fstep1;
      }
      // in the descending part of the triangle...
      else if ((binfreq >= _filterFrequencies[i+1]) && (binfreq < _filterFrequencies[i+2])) {
        _filterCoefficients[i][j] = (hz2mel(_filterFrequencies[i+2]) - hz2mel(binfreq)) / fstep2;
      }
    }
  }

  // normalize the filter weights
  for (int i=0; i<filterSize; ++i) {
    Real weight = 0.0;

    for (int j=0; j<spectrumSize; ++j) {
      weight += _filterCoefficients[i][j];
    }

    if (weight == 0) continue;

    for (int j=0; j<spectrumSize; ++j) {
      _filterCoefficients[i][j] = _filterCoefficients[i][j] / weight;
    }
  }
}

void MelBands::compute() {
  const std::vector<Real>& spectrum = _spectrumInput.get();
  std::vector<Real>& bands = _bandsOutput.get();

  int filterSize = _numBands;
  int spectrumSize = spectrum.size();

  if (_filterCoefficients.empty() || int(_filterCoefficients[0].size()) != spectrumSize) {
      E_INFO("MelBands: input spectrum size (" << spectrumSize << ") does not correspond to the \"inputSize\" parameter (" << _filterCoefficients[0].size() << "). Recomputing the filter bank.");
    createFilters(spectrumSize);
  }

  // calculate all the bands
  bands.resize(filterSize);

  Real frequencyScale = (_sampleRate / 2.0) / (spectrumSize - 1);

  // apply the filters
  for (int i=0; i<filterSize; ++i) {
    bands[i] = 0;

    int jbegin = int(_filterFrequencies[i] / frequencyScale + 0.5);
    int jend = int(_filterFrequencies[i+2] / frequencyScale + 0.5);

    for (int j=jbegin; j<jend; ++j) {
      bands[i] += (spectrum[j] * spectrum[j]) * _filterCoefficients[i][j];
    }
  }
}