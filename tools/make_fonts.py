"""한글 TTF 글꼴을 LVGL v8 용 C 글꼴 파일로 바꿉니다.

사용법:  python tools/make_fonts.py
결과물:  firmware/main/fonts/*.c, fonts_kr.h, 그리고 확인용 tools/font_preview.png
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT_DIR = ROOT / "tools" / "fonts"
OUT_DIR = ROOT / "firmware" / "main" / "fonts"

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
# KS X 1001 완성형 한글 2350자 (자주 쓰는 글자 대부분)
HANGUL = "".join(
    bytes([b1, b2]).decode("euc-kr")
    for b1 in range(0xB0, 0xC9)
    for b2 in range(0xA1, 0xFF)
)
EXTRA = "·°℃~※→←↑↓…★☆○●◆■□△▲▽▼「」『』《》〈〉【】①②③④⑤⑥⑦⑧⑨"
FULL = ASCII + HANGUL + EXTRA
DATE = "0123456789 ,.:년월일요화수목금토오전후늘내"

# (C 이름, TTF, 크기 px, 글자 모음, 기호용 대체 글꼴)
FONTS = [
    ("font_kr_18", "NanumGothic-Regular.ttf", 18, FULL, "lv_font_montserrat_16"),
    ("font_kr_22", "NanumGothic-Regular.ttf", 22, FULL, "lv_font_montserrat_20"),
    ("font_kr_26", "NanumGothic-Bold.ttf", 26, FULL, "lv_font_montserrat_24"),
    ("font_kr_36", "NanumGothic-Bold.ttf", 36, DATE, None),
    ("font_num_60", "NanumGothic-Bold.ttf", 60, "0123456789-.°% ", None),
    ("font_num_130", "NanumGothic-ExtraBold.ttf", 130, "0123456789:- ", None),
]

BPP = 4


def render(font, ch):
    """한 글자를 그려서 (그림, x0, y0, 너비) 를 돌려줍니다. 좌표는 기준선 기준."""
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    adv = font.getlength(ch)
    w, h = max(x1 - x0, 1), max(y1 - y0, 1)
    img = Image.new("L", (w + 4, h + 4), 0)
    ImageDraw.Draw(img).text((2 - x0, 2 - y0), ch, font=font, fill=255, anchor="ls")
    box = img.getbbox()
    if box is None:
        return None, 0, 0, adv
    img = img.crop(box)
    return img, x0 - 2 + box[0], y0 - 2 + box[1], adv


def pack(img):
    """픽셀을 4비트씩 빈틈없이 이어 붙입니다 (LVGL bitmap_format 0)."""
    vals = [round(v * 15 / 255) for v in img.getdata()]
    out = bytearray()
    for i in range(0, len(vals), 2):
        hi = vals[i]
        lo = vals[i + 1] if i + 1 < len(vals) else 0
        out.append((hi << 4) | lo)
    return out


def unpack(data, w, h):
    vals = []
    for b in data:
        vals += [b >> 4, b & 15]
    return vals[: w * h]


def build(name, ttf, size, chars, fallback):
    font = ImageFont.truetype(str(FONT_DIR / ttf), size)
    notdef, *_ = render(font, "￿")
    notdef_bytes = notdef.tobytes() if notdef else None

    ascent, descent = font.getmetrics()
    glyphs = []  # (cp, adv16, w, h, ofs_x, ofs_y, bytes)
    for ch in sorted(set(chars)):
        img, x0, y0, adv = render(font, ch)
        if ch != " " and img is not None and notdef_bytes and img.tobytes() == notdef_bytes:
            continue  # 글꼴에 없는 글자
        adv16 = round(adv * 16)
        if img is None:
            glyphs.append((ord(ch), adv16, 0, 0, 0, 0, b""))
            continue
        w, h = img.size
        ofs_y = -(y0 + h)  # 기준선에서 상자 아래쪽까지 (위쪽이 +)
        glyphs.append((ord(ch), adv16, w, h, x0, ofs_y, bytes(pack(img))))

    bitmap = bytearray()
    dscs = ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}"]
    for cp, adv16, w, h, ox, oy, data in glyphs:
        assert len(bitmap) < (1 << 20), f"{name}: 그림 자료가 1MB 를 넘습니다"
        assert adv16 < 4096 and -128 <= ox < 128 and -128 <= oy < 128 and w < 256 and h < 256
        dscs.append(f"    {{.bitmap_index = {len(bitmap)}, .adv_w = {adv16}, .box_w = {w}, "
                    f".box_h = {h}, .ofs_x = {ox}, .ofs_y = {oy}}}")
        bitmap += data

    first = glyphs[0][0]
    offsets = [cp - first for cp, *_ in glyphs]
    assert offsets[-1] < 65536

    def rows(seq, per):
        seq = list(seq)
        return ",\n".join("    " + ", ".join(seq[i:i + per]) for i in range(0, len(seq), per))

    src = f"""/* tools/make_fonts.py 가 만든 파일입니다. 직접 고치지 마세요.
 * 원본 글꼴: {ttf} ({size}px, {len(glyphs)}자) - SIL Open Font License 1.1 */
#include "lvgl.h"

static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {{
{rows((f"0x{b:02x}" for b in bitmap), 24)}
}};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{
{",\n".join(dscs)}
}};

static const uint16_t unicode_list[] = {{
{rows((str(o) for o in offsets), 16)}
}};

static const lv_font_fmt_txt_cmap_t cmaps[] = {{
    {{
        .range_start = {first}, .range_length = {offsets[-1] + 1}, .glyph_id_start = 1,
        .unicode_list = unicode_list, .glyph_id_ofs_list = NULL, .list_length = {len(offsets)},
        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }}
}};

static lv_font_fmt_txt_glyph_cache_t cache;

static const lv_font_fmt_txt_dsc_t font_dsc = {{
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = {BPP},
    .kern_classes = 0,
    .bitmap_format = 0,
    .cache = &cache
}};

const lv_font_t {name} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {ascent + descent},
    .base_line = {descent},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2,
    .underline_thickness = 1,
    .dsc = &font_dsc,
    .fallback = {"&" + fallback if fallback else "NULL"},
    .user_data = NULL
}};
"""
    (OUT_DIR / f"{name}.c").write_text(src, encoding="utf-8")
    print(f"{name}: {len(glyphs)}자, 그림 {len(bitmap) / 1024:.0f} KB")
    return dict(glyphs={g[0]: g for g in glyphs}, bitmap=bitmap, line=ascent + descent, base=descent)


def preview(fonts):
    """만든 자료를 거꾸로 풀어서 그려 봅니다 (변환이 맞는지 눈으로 확인)."""
    samples = [
        ("font_num_130", "09:41"),
        ("font_kr_36", "11월 2일 월요일"),
        ("font_num_60", "-3.5°"),
        ("font_kr_26", "오늘 급식 · 시간표 · 일정"),
        ("font_kr_22", "흑미밥, 쇠고기미역국, 닭갈비, 배추김치"),
        ("font_kr_18", "Google 캘린더 오후 3:00 부서 회의 ℃"),
    ]
    W = 1024
    H = sum(fonts[n]["line"] + 8 for n, _ in samples)
    img = Image.new("L", (W, H), 20)
    y = 0
    for n, text in samples:
        f = fonts[n]
        pen = 10.0
        base_y = y + f["line"] - f["base"]
        for ch in text:
            g = f["glyphs"].get(ord(ch))
            if g is None:
                pen += 10
                continue
            cp, adv16, w, h, ox, oy, data = g
            vals = unpack(data, w, h)
            for j in range(h):
                for i in range(w):
                    v = vals[j * w + i] * 17
                    px, py = int(pen) + ox + i, base_y - oy - h + j
                    if 0 <= px < W and 0 <= py < H and v:
                        img.putpixel((px, py), max(img.getpixel((px, py)), v))
            pen += adv16 / 16
        y += f["line"] + 8
    img.save(ROOT / "tools" / "font_preview.png")


if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    built = {name: build(name, ttf, size, chars, fb) for name, ttf, size, chars, fb in FONTS}
    header = ["#pragma once", '#include "lvgl.h"', ""]
    header += [f"LV_FONT_DECLARE({name})" for name, *_ in FONTS]
    (OUT_DIR / "fonts_kr.h").write_text("\n".join(header) + "\n", encoding="utf-8")
    preview(built)
