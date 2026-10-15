#pragma once
inline bool eliteTheme = false;
inline D2D1::ColorF ThemeColor(UINT32 rgb, float alpha = 1.f)
{
    if (eliteTheme)
        switch (rgb)
        {
        case 0x101b2b:
            rgb = 0x100e0c;
            break;
        case 0x172b40:
        case 0x182738:
        case 0x202833:
            rgb = 0x1c1711;
            break;
        case 0x22374e:
        case 0x1c2f44:
            rgb = 0x302215;
            break;
        case 0x29465e:
        case 0x30475d:
        case 0x304a60:
        case 0x3b526b:
            rgb = 0x594027;
            break;
        case 0x375c7c:
        case 0x315979:
        case 0x164f70:
            rgb = 0x6b350b;
            break;
        case 0x396385:
        case 0x347fa7:
            rgb = 0x844817;
            break;
        case 0x55d4ff:
        case 0xffbd55:
            rgb = 0xff8c22;
            break;
        case 0xeaf1ff:
            rgb = 0xffcf94;
            break;
        case 0xa8c0d8:
        case 0x7194af:
        case 0x75808d:
            rgb = 0xc9a477;
            break;
        case 0x995a20:
            rgb = 0xa35316;
            break;
        case 0x985c48:
            rgb = 0xba5836;
            break;
        }
    return D2D1::ColorF(rgb, alpha);
}
