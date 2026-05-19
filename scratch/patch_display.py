import re

file_path = "firmware/v6_doit30_clean_rewrite/main/display/display_ctrl.c"
with open(file_path, "r") as f:
    content = f.read()

# Replace eye styles
content = content.replace("EYE_STYLE_OPEN", "EYE_STYLE_CIRCLE")
content = content.replace("EYE_STYLE_WIDE", "EYE_STYLE_CIRCLE")
content = content.replace("EYE_STYLE_SQUINT", "EYE_STYLE_CIRCLE_SQUINT")
content = content.replace("EYE_STYLE_CLOSED", "EYE_STYLE_CIRCLE_CLOSED")

# Mask mouths: corners to 0
# The mask removes bits: 
# Row 0: 0x8001 (leftmost and rightmost)
# Row 1: 0x0000 (none)
# Row 6: 0xC003 (2 leftmost and 2 rightmost)
# Row 7: 0xE007 (3 leftmost and 3 rightmost)
# Wait, matrix bits are MSB to LSB. 0x8001 -> bit 15 and bit 0.

def mask_mouth_line(match):
    lines = match.group(0).split('\n')
    for i, line in enumerate(lines):
        if "{" in line and "}" in line:
            # find all 0x...
            hexes = re.findall(r'0x[0-9A-Fa-f]{4}', line)
            if len(hexes) == 8:
                vals = [int(h, 16) for h in hexes]
                # mask corners
                vals[0] &= ~0xC003
                vals[1] &= ~0x8001
                vals[6] &= ~0x8001
                vals[7] &= ~0xE007
                new_hexes = [f"0x{v:04X}" for v in vals]
                # rebuild line
                new_line = "    {" + ", ".join(new_hexes) + "},"
                lines[i] = new_line
    return "\n".join(lines)

content = re.sub(r'static const uint16_t MOUTH_[A-Z]+\w*\[\]\[8\] = \{.*?\};', mask_mouth_line, content, flags=re.DOTALL)

# Add EYE_STYLE_CIRCLE etc to enum
content = content.replace("    EYE_STYLE_OPEN,", "    EYE_STYLE_CIRCLE,\n    EYE_STYLE_CIRCLE_SQUINT,\n    EYE_STYLE_CIRCLE_CLOSED,")
content = content.replace("    EYE_STYLE_WIDE,\n", "")
content = content.replace("    EYE_STYLE_SQUINT,\n", "")
content = content.replace("    EYE_STYLE_CLOSED,\n", "")

# Update draw_eye_shape
old_draw = """    switch (profile->style) {
        case EYE_STYLE_CLOSED:
            u8g2_DrawRBox(u8g2, EYE_BASE_X, EYE_CENTER_Y - 2, 80, 5, 2);
            return;

        case EYE_STYLE_SQUINT:
            u8g2_DrawRBox(u8g2, x, y, profile->width, profile->height, profile->height / 2);
            break;

        case EYE_STYLE_WIDE:
            u8g2_DrawRBox(u8g2, x, y, profile->width, profile->height, profile->height / 2);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawBox(u8g2, x - 1, y - 3, profile->width + 2, 3);
            u8g2_DrawBox(u8g2, x - 1, y + profile->height, profile->width + 2, 3);
            u8g2_SetDrawColor(u8g2, 1);
            break;

        case EYE_STYLE_OPEN:
        default:
            u8g2_DrawRBox(u8g2, x, y, profile->width, profile->height, profile->height / 2);
            break;
    }"""

new_draw = """    const int radius = profile->width / 2;
    switch (profile->style) {
        case EYE_STYLE_CIRCLE_CLOSED:
            u8g2_DrawLine(u8g2, EYE_CENTER_X - radius, EYE_CENTER_Y, EYE_CENTER_X + radius, EYE_CENTER_Y);
            u8g2_DrawLine(u8g2, EYE_CENTER_X - radius, EYE_CENTER_Y + 1, EYE_CENTER_X + radius, EYE_CENTER_Y + 1);
            return;

        case EYE_STYLE_CIRCLE_SQUINT:
            u8g2_DrawDisc(u8g2, EYE_CENTER_X, EYE_CENTER_Y, radius, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
            break;

        case EYE_STYLE_CIRCLE:
        default:
            u8g2_DrawDisc(u8g2, EYE_CENTER_X, EYE_CENTER_Y, radius, U8G2_DRAW_ALL);
            break;
    }"""

content = content.replace(old_draw, new_draw)

# Thick eyebrows
old_brow = """    for (int offset = 0; offset < BROW_THICK; offset++) {
        u8g2_DrawLine(u8g2, outer_x, outer_y + offset, inner_x, inner_y + offset);
    }"""

new_brow = """    for (int offset = -1; offset <= BROW_THICK; offset++) {
        u8g2_DrawLine(u8g2, outer_x, outer_y + offset, inner_x, inner_y + offset);
    }"""

content = content.replace(old_brow, new_brow)

with open(file_path, "w") as f:
    f.write(content)
print("Updated display_ctrl.c")
