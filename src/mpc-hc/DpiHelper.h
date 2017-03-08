/*
 * (C) 2015-2016 see Authors.txt
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

#pragma once

class DpiHelper final
{
public:

    DpiHelper();

    void Override(HWND hWindow);
    void Override(int dpix, int dpiy);

    inline double ScaleFactorX() const { return m_dpix / 96.0; }
    inline double ScaleFactorY() const { return m_dpiy / 96.0; }

    inline int ScaleFloorX(int x) const { return x * m_dpix / 96; }
    inline int ScaleFloorY(int y) const { return y * m_dpiy / 96; }
    inline int ScaleX(int x) const { return MulDiv(x, m_dpix, 96); }
    inline int ScaleY(int y) const { return MulDiv(y, m_dpiy, 96); }
    inline int TransposeScaledX(int x) const { return MulDiv(x, m_dpiy, m_dpix); }
    inline int TransposeScaledY(int y) const { return MulDiv(y, m_dpix, m_dpiy); }

    inline int ScaleSystemToOverrideX(int x) const { return MulDiv(x, m_dpix, m_sdpix); }
    inline int ScaleSystemToOverrideY(int y) const { return MulDiv(y, m_dpiy, m_sdpiy); }

private:

    int m_dpix, m_dpiy;
    int m_sdpix, m_sdpiy;
};
