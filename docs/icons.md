# The game's icons (for the next step: drawn from the player's own game files)

Atlas: resident.arc, GUI\99_texture\HD_cmn_icon_GSM_NOMIP (512x1024, grey, tinted by the game in the rarity's colour).
Large icons: 48 x 48 cells, cell (row r, column c) at x = 48c, y = 416 + 48r (the 32 px set above, y < 416).

| weapon type (XX id) | cell | | armor piece | cell |
|---|---|---|---|---|
| 0 GS | 5,5 | | head | 7,0 |
| 1 SnS | 5,7 | | body | 7,1 |
| 2 Hammer | 5,9 | | arms | 7,3 |
| 3 Lance | 6,1 | | waist | 7,2 |
| 4 HBG | 6,5 | | legs | 7,4 |
| 6 LBG | 6,3 | | | |
| 7 LS | 5,6 | | | |
| 8 SA | 6,7 | | | |
| 9 GL | 6,2 | | | |
| 10 Bow | 6,6 | | | |
| 11 DB | 5,8 | | | |
| 12 HH | 6,0 | | | |
| 13 IG | 6,8 | | | |
| 14 CB | 6,9 | | | |

Next: a port HLE that loads a game texture from the romfs (arc + .tex, decoded by runtime/nvn) as RGBA for a plugin's
ImTextureData; the mod then draws these cells tinted (ImDrawList_AddImage with the rarity colour).
