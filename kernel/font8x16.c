#include "font8x16.h"

const uint8_t *font_get_glyph(uint8_t glyph) {
  return ((const uint8_t *)_binary_font8x16_bin_start) +
         ((uint32_t)glyph * 16);
}

// Decodes a UTF-8 character byte stream into a CP437 glyph index.
uint8_t utf8_decode_to_cp437(const uint8_t **stream) {
  const uint8_t *s = *stream;
  uint8_t b0 = *s;

  if (b0 < 0x80) {
    (*stream)++;
    return b0;
  }

  // 2-byte UTF-8 sequence (110xxxxx 10xxxxxx)
  if ((b0 & 0xE0) == 0xC0) {
    uint8_t b1 = s[1];
    if ((b1 & 0xC0) == 0x80) {
      uint32_t codepoint = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
      *stream += 2;

      // Small-caps mapping for dmash banner
      switch (codepoint) {
      case 0x00D8: // Ø (Slashed O)
      case 0x00F8: // ø
        return 0xED;
      case 0x029F: // ʟ (Small capital L)
        return 'l';
      case 0x029C: // ʜ (Small capital H)
        return 'h';
      default:
        break;
      }
      return 0xFE; // Default fallback glyph
    }
  }

  // 3-byte UTF-8 sequence (1110xxxx 10xxxxxx 10xxxxxx)
  if ((b0 & 0xF0) == 0xE0) {
    uint8_t b1 = s[1];
    uint8_t b2 = s[2];
    if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
      uint32_t codepoint =
          ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
      *stream += 3;

      // Empty set / Diameter symbol in dmash prompt (∅ U+2205)
      if (codepoint == 0x2205 || codepoint == 0x2300 || codepoint == 0x2298) {
        return 0xED;
      }

      // Small-caps banner characters in dmash.cfg (ᴡᴇʟᴄᴏᴍᴇ ᴛᴏ ᴅᴍᴀsʜ)
      switch (codepoint) {
      case 0x1D21: return 'W'; // ᴡ
      case 0x1D07: return 'E'; // ᴇ
      case 0x1D04: return 'C'; // ᴄ
      case 0x1D0F: return 'O'; // ᴏ
      case 0x1D0D: return 'M'; // ᴍ
      case 0x1D1B: return 'T'; // ᴛ
      case 0x1D05: return 'D'; // ᴅ
      case 0x1D00: return 'A'; // ᴀ
      case 0x1D1C: return 'U'; // ᴜ
      case 0x1D18: return 'P'; // ᴘ
      default:
        break;
      }

      // UTF-8 Box Drawing (U+2500 - U+257F) -> CP437 mappings
      if (codepoint >= 0x2500 && codepoint <= 0x257F) {
        switch (codepoint) {
        case 0x2500: return 0xC4; // ─
        case 0x2502: return 0xB3; // │
        case 0x250C: return 0xDA; // ┌
        case 0x2510: return 0xBF; // ┐
        case 0x2514: return 0xC0; // └
        case 0x2518: return 0xD9; // ┘
        case 0x251C: return 0xC3; // ├
        case 0x2524: return 0xB4; // ┤
        case 0x252C: return 0xC2; // ┬
        case 0x2534: return 0xC1; // ┴
        case 0x253C: return 0xC5; // ┼
        case 0x2550: return 0xCD; // ═
        case 0x2551: return 0xBA; // ║
        case 0x2554: return 0xC9; // ╔
        case 0x2557: return 0xBB; // ╗
        case 0x255A: return 0xC8; // ╚
        case 0x255D: return 0xBC; // ╝
        case 0x2560: return 0xCC; // ╠
        case 0x2563: return 0xB9; // ╣
        case 0x2566: return 0xCB; // ╦
        case 0x2569: return 0xCA; // ╩
        case 0x256C: return 0xCE; // ╬
        default:
          return 0xC4;
        }
      }

      // UTF-8 Block Elements (U+2580 - U+259F)
      if (codepoint >= 0x2580 && codepoint <= 0x259F) {
        switch (codepoint) {
        case 0x2588: return 0xDB; // █
        case 0x2584: return 0xDC; // ▄
        case 0x258C: return 0xDD; // ▌
        case 0x2590: return 0xDE; // ▐
        case 0x2580: return 0xDF; // ▀
        case 0x2591: return 0xB0; // ░
        case 0x2592: return 0xB1; // ▒
        case 0x2593: return 0xB2; // ▓
        default:
          return 0xDB;
        }
      }

      return 0xFE;
    }
  }

  // Fallback for unrecognized multi-byte sequence
  (*stream)++;
  return b0;
}
