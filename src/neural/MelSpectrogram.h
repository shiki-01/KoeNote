#pragma once

#include <cstddef>
#include <vector>

namespace koenote::neural
{
// ニューラルボコーダー（NSF-HiFiGAN 系）が入力に使うメルスペクトログラムの設定。
// 既定値は PC-NSF-HiFiGAN 44.1k hop512 128bin（vocoder.yaml の値）。
struct MelConfig
{
    double sampleRate = 44100.0;
    int hopSize = 512;
    int winSize = 2048;
    int fftSize = 2048;
    int numMelBins = 128;
    double fmin = 40.0;
    double fmax = 16000.0;
};

// 対数メルスペクトログラム（自然対数、Slaney のメル尺度と正規化）。
// フレーム j は、波形の [j*hop, (j+1)*hop) に対応する（反射パディング (fft - hop)/2 を両端に付ける）。
// 戻り値は frames * numMelBins の行優先配列。波形が空なら空。
std::vector<float> computeLogMel(const std::vector<float>& wave, const MelConfig& config, std::size_t& numFrames);

// メル周波数軸の変換（Slaney）。フォルマントシフトのための周波数軸のずらしに使う。
double hzToMel(double hz) noexcept;
double melToHz(double mel) noexcept;

// 各メルビンの中心周波数（Hz）。numMelBins 個。
std::vector<double> melBinCentreHz(const MelConfig& config);

// メル 1 フレーム（numMelBins 個）の周波数軸を、factor 倍にずらす（factor > 1 で上へ）。
// new[b] = old[周波数 f_b / factor に対応する位置]（線形補間、範囲外は端の値）。
void shiftMelFrame(const float* in, float* out, const std::vector<double>& centreHz, double factor);
} // namespace koenote::neural
