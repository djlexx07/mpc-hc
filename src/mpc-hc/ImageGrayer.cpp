/*
* (C) 2016 see Authors.txt
*
* This file is part of MPC-HC.
*
* MPC-HC is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* MPC-HC is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/

#include "stdafx.h"
#include "ImageGrayer.h"

struct HLS {
    double H, L, S;

    HLS(const RGBQUAD& rgb) {
        double R = rgb.rgbRed / 255.0;
        double G = rgb.rgbGreen / 255.0;
        double B = rgb.rgbBlue / 255.0;
        double max = std::max({ R, G, B });
        double min = std::min({ R, G, B });

        L = (max + min) / 2.0;

        if (max == min) {
            S = H = 0.0;
        } else {
            double d = max - min;

            S = (L < 0.5) ? (d / (max + min)) : (d / (2.0 - max - min));

            if (R == max) {
                H = (G - B) / d;
            } else if (G == max) {
                H = 2.0 + (B - R) / d;
            } else { // if (B == max)
                H = 4.0 + (R - G) / d;
            }
            H /= 6.0;
            if (H < 0.0) {
                H += 1.0;
            }
        }
    }

    RGBQUAD toRGBQUAD() {
        RGBQUAD rgb;
        rgb.rgbReserved = 255;

        if (S == 0.0) {
            rgb.rgbRed = rgb.rgbGreen = rgb.rgbBlue = BYTE(L * 255);
        } else {
            auto hue2rgb = [](double p, double q, double h) {
                if (h < 0.0) {
                    h += 1.0;
                } else if (h > 1.0) {
                    h -= 1.0;
                }

                if (h < 1.0 / 6.0) {
                    return p + (q - p) * 6.0 * h;
                } else if (h < 0.5) {
                    return q;
                } else if (h < 2.0 / 3.0) {
                    return p + (q - p) * (2.0 / 3.0 - h) * 6.0;
                }
                return p;
            };

            double q = (L < 0.5) ? (L * (1 + S)) : (L + S - L * S);
            double p = 2 * L - q;

            rgb.rgbRed = BYTE(hue2rgb(p, q, H + 1.0 / 3.0) * 255);
            rgb.rgbGreen = BYTE(hue2rgb(p, q, H) * 255);
            rgb.rgbBlue = BYTE(hue2rgb(p, q, H - 1.0 / 3.0) * 255);
        }

        return rgb;
    }
};

bool ImageGrayer::Gray(const CImage& imgSource, CImage& imgDest)
{
    // Only support 32-bit image for now
    if (imgSource.GetBPP() != 32) {
        return false;
    }

    imgDest.Destroy();
    if (!imgDest.Create(imgSource.GetWidth(), imgSource.GetHeight(), imgSource.GetBPP())) {
        return false;
    }
    BOOL bCopied = imgSource.BitBlt(imgDest.GetDC(), 0, 0);
    imgDest.ReleaseDC();
    if (!bCopied) {
        return false;
    }

    auto adjustBrightness = [](BYTE c, double p) {
        int cAdjusted;
        if (c == 0 && p > 1.0) {
            cAdjusted = std::lround((p - 1.0) * 255);
        } else {
            cAdjusted = std::lround(c * p);
        }

        return BYTE(std::min(cAdjusted, 255));
    };

    BYTE* bits = static_cast<BYTE*>(imgDest.GetBits());
    for (int y = 0; y < imgDest.GetHeight(); y++, bits += imgDest.GetPitch()) {
        RGBQUAD* p = reinterpret_cast<RGBQUAD*>(bits);
        for (int x = 0; x < imgDest.GetWidth(); x++) {
            HLS hls(p[x]);
            hls.S = 0.0; // Make the color gray

            RGBQUAD rgb = hls.toRGBQUAD();

            p[x].rgbRed = BYTE(adjustBrightness(rgb.rgbRed, 1.5) * p[x].rgbReserved / 255);
            p[x].rgbGreen = BYTE(adjustBrightness(rgb.rgbGreen, 1.5) * p[x].rgbReserved / 255);
            p[x].rgbBlue = BYTE(adjustBrightness(rgb.rgbBlue, 1.5) * p[x].rgbReserved / 255);
        }
    }

    return true;
}
