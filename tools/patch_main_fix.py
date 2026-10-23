import os

# Read current main.c
with open('port3ds/source/main.c', 'r', encoding='utf-8') as f:
    text = f.read()

# 1. Add forward declaration for play_level_theme near top
fwd = 'static void play_level_theme(int level_id);\n'
if fwd not in text:
    anchor = 'static void start_entrance(void);'
    text = text.replace(anchor, fwd + anchor)

# 2. Fix printf format warnings with (unsigned) casts
text = text.replace('snprintf(t_str, sizeof(t_str), "%02u:%02u:%02u", mins, secs, cs);',
                    'snprintf(t_str, sizeof(t_str), "%02u:%02u:%02u", (unsigned)mins, (unsigned)secs, (unsigned)cs);')

text = text.replace('snprintf(tt_txt, sizeof(tt_txt), "BEST TIME: %02u:%02u:%02u", mins, secs, cs);',
                    'snprintf(tt_txt, sizeof(tt_txt), "BEST TIME: %02u:%02u:%02u", (unsigned)mins, (unsigned)secs, (unsigned)cs);')

# 3. Fix the else clause formatting
old_else = '''                if (g_save.completion_pct == 0)
                    s_state = STATE_INTRO;
                else
                    s_state = STATE_WARP_ROOM; audio_play_bgm(BGM_WARP_ROOM);'''
new_else = '''                if (g_save.completion_pct == 0) {
                    s_state = STATE_INTRO;
                } else {
                    s_state = STATE_WARP_ROOM;
                    audio_play_bgm(BGM_WARP_ROOM);
                }'''

if old_else not in text:
    old_else = old_else.replace('\n', '\r\n')
    new_else = new_else.replace('\n', '\r\n')

if old_else in text:
    text = text.replace(old_else, new_else)

with open('port3ds/source/main.c', 'w', encoding='utf-8') as f:
    f.write(text)

print('Updated main.c fixes!')
