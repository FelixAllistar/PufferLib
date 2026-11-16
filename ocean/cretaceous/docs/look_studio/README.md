# Look studio

The Look studio is a panel down the right side of the live game. It holds every knob of the picture, and it saves what you set as a "look".

## Opening it

- **In play:** press **F3**. Press F3 again, press Esc or click Close to shut it.
- **From Settings:** use the **Look studio** row. It shows the look in play. When you close the studio from there, you go back to Settings.

While the panel is open:
- the mouse is free;
- the HUD is hidden, so you judge the picture alone;
- holding the **right mouse button** over the scene and dragging turns the camera;
- the wheel zooms over the scene and scrolls over the panel;
- your animal stands still, but the world keeps running;
- **F** still gives the free camera, and WASD moves it.

The panel scales with the window, from 1280x720 to 4K. It is desktop only. The web build has no panel, but the look you chose still applies there.

## Using it

- **Sliders:** drag them. The small tick on each track marks the default, and the slider snaps to it as you pass.
  - A **double-click** or a **right-click** puts one slider back to its default.
  - Each section has a **Reset** button.
  - A changed value turns amber, and its name brightens.
- **Help line:** hover any row to read what it does on the line at the bottom of the panel.
- **Sections:** click a heading to fold it or unfold it.
- **Split on:** compares two looks side by side. The left side shows **Before**, which is the look you had when you first opened the studio this session. Click **Left: Before** to switch the left side to **Default**, which is the game as shipped. The right side shows the sliders as they are now. Drag the line to move the divider.
- **The dot rule:** a knob with a small dot after its name changes how the 3D scene is lit (sun, haze, foliage colour). There is only one scene per frame, so the split cannot show those knobs.
- **Hold: before** (press and hold the button, or hold **B**): shows the whole frame with the Before look, lighting included, for as long as you hold it.
- **Screenshot:** saves the frame at full window resolution, without the panel, as `screenshots/look_DATE.png` in the game's folder. It also saves the look's settings beside it as `look_DATE.ini`.
- **Scene:** controls for judging a look in different conditions: time of day, cloud cover, weather (from clouds, clear, overcast, rain) and a frozen clock. These change the world itself, so they work in solo play and when you host, but not when you join someone else's game.

## Looks

A look is a set of slider values with a name.

- Click a look's name to load it.
- **Save as new** asks for a name and saves the sliders as a look of your own.
- **Save**, **Rename** and **Delete** act on your own looks. The shipped looks cannot be overwritten. Save a copy with Save as new instead.
- The look in play is always kept. It applies at once, on every quality preset, and comes back the next time you start the game.

Looks are small text files next to `settings.ini`:
- On Windows: `%APPDATA%\Cretaceous`.
- On Linux: `~/.config/cretaceous`.

Inside that folder:
- `look.ini` is the look in play.
- `looks/NAME.ini` are the looks you saved.

To send a look to the developer, send its `.ini` file, or the `.ini` that a screenshot saved. Each line is one knob, for example `saturation = 1.08`. A knob that is missing from the file keeps its default.

## Shipped looks

These are compared in the contact sheets below.

- **Default:** the game exactly as it was before the studio. Every knob is at its default.
- **Richer:** your hunch, a little more colour.
  - Vibrance does most of the work. It strengthens the dull colours (grey-green leaves, soil, haze) and leaves already vivid colours alone, including the sunset, the sun's warm light, blood and the dinosaurs' skin.
  - On top of that: saturation +8%, greens +4%, and a touch more contrast.
- **Filmic:** the AgX tone curve, as in Blender and recent engines.
  - Bright light rolls off to white gently and keeps its hue, so a sunlit sky or a lit leaf does not bleach toward cream.
  - On its own AgX reads grey and flat, so this look adds contrast, saturation and deeper blacks. The result is the richest, most "cinema" look of the set.
- **Overcast:** for grey days. More contrast and vibrance, a slightly warmer white balance and less rain haze, so overcast country does not go flat and blue-grey. It is also fine in sun, just punchier.
- **Golden** (mine): warm late light.
  - The sun is warmer and the air a little clearer.
  - Shadows lean slightly blue and highlights slightly gold, the classic warm-and-cool split.
  - It suits the island's ochre soils and the evening. It is a look for sunny days: under a grey sky it turns the horizon cream.
- **Crisp** (mine): clear air (less haze), deeper greens, slightly cool shadows and a stronger sharpen. It is the most "high definition" of the set.

## The knobs, in plain words

### Exposure and tone

- **Exposure:** brightens or darkens everything, in stops (+1 is twice as bright).
- **Eye adaptation:** how far the exposure follows what you look at.
  - At 0 the exposure only follows the sun.
  - At 1 (default) it brightens a dark forest floor and darkens a bright sky, as the game always has.
- **Adaptation speed:** how quickly it adjusts when you turn from dark to bright.
- **Night darkening:** how much darker the moonlit night is drawn (default 1.8 stops).
- **Tone curve:** how bright light rolls off to white.
  - *Game (ACES):* the game's own curve.
  - *AgX:* softer, and keeps the hue of bright colours.
  - *PBR Neutral:* the truest colours up to the highlights, then a quick roll-off.
  - *Hable:* the classic filmic curve, gentle in the shadows.
  - *Reinhard:* very soft and low in contrast.
  - Every curve is matched so that mid-grey comes out the same, so switching changes the character of the image, not its brightness.
- **White point:** lower it and highlights reach pure white sooner, which gives a punchier, more clipped look. At its default (11.2, the highest setting) nothing changes.
- **Contrast:** the game's daytime S-curve: darker shadows and brighter lit surfaces. Below 0 the image flattens.
- **Toe:** to the right, deeper and richer blacks; to the left, lifted, milky blacks.
- **Shoulder:** to the right, brighter highlights that clip sooner; to the left, softer highlights.

### Colour

- **Saturation:** the strength of every colour.
- **Vibrance:** the strength of dull colours only. Skin, oranges and already vivid colours are protected. This is the safer way to get "more colour".
- **Temperature, Tint:** white balance. Temperature runs from cooler (blue) to warmer (amber). Tint runs from greener to more magenta.
- **Shadows, Midtones, Highlights:** the classic lift, gamma and gain.
  - Each has an amount that raises or lowers that range.
  - Each also has a colour: pick the hue with "... colour", and set how far to lean toward it with "... colour amount".
  - Two common uses: cool shadows (a blue hue on Shadows) and warm highlights (a gold hue on Highlights).
- **Warm light, cool shade:** the game's existing grade. It makes sunlit tones a touch warm, shade a touch cool, and lifts the shaded mid-tones. 0 turns it off, 2 doubles it.
- **Greens: hue, Greens: saturation:** act on green pixels only, since the island is mostly green. The hue slider runs from yellower to bluer greens.

### Effects

- **Bloom:** the glow around bright light. Threshold sets how bright light must be before it glows, and radius sets how far the glow spreads.
- **Sharpen:** crispness of texture. It only applies on the high and ultra presets at full resolution, as before.
- **Vignette:** darker corners.
- **Contact shade:** the screen-space ambient occlusion, which shades creases and the places where things meet, such as feet on the ground and rocks in grass. Radius sets how far that shade reaches. It applies on the high and medium presets; low has none.
- **Film grain:** a fine moving grain. It is off by default.

### Atmosphere

- **Haze:** how quickly distance fades into the sky colour. This includes the low valley mist.
- **Haze colour:** from bluer to warmer haze.
- **Forest mist:** the pale mist between trunks when you stand inside a conifer forest.
- **Sky brightness:** the sky you see. It does not change the light the sky gives.
- **Cloud brightness:** how bright the clouds are drawn.
- **Rain haze:** the extra haze under overcast and rain.

### Light

- **Sunlight:** the strength of direct sun.
- **Sun colour:** from a whiter, cooler sun to a warmer one.
- **Sky light:** the light from the sky, which fills shade and haze. It also brightens the sky itself, so use Sky brightness to set the sky back if you want only the fill.
- **Ground bounce:** sunlight bounced up from the ground onto plants and steep slopes.
- **Shadow softness:** wider, softer shadow edges. It costs nothing extra.
- **Moonlight:** the strength of the moon at night.

### Foliage

- **Leaf saturation, Leaf brightness, Leaf hue:** the colour of leaves and fronds on every plant. Bark is unchanged.
- **Leaf backlight:** the yellow-green glow of sun shining through leaves when you look toward the sun.
- **Ground cover saturation:** the green ground cover that the terrain paints past the drawn ferns.

## The knobs that matter most

1. **Vibrance**, then **Saturation**. Vibrance is your "a bit more colour" without neon greens or orange skin.
2. **Tone curve** (try AgX) with **Contrast**. These two change the whole character of the picture.
3. **Temperature** and **Warm light, cool shade**. These set the mood.
4. **Haze**. With less haze the island reads sharper and bigger; with more, it reads deeper and more atmospheric.
5. **Leaf saturation** and **Greens: hue**. Most of the screen is leaves, so small moves here matter.

Exposure is best left near 0, since eye adaptation already handles it. Bloom and Sharpen are already tuned.

## Contact sheets

The sheets below show every shipped look on the same view. In this cloud sandbox there were no dinosaur models, so the animals in the grassland view are the stand-in spheres.

- `sheet_forest.jpg`: the forest floor.
- `sheet_grassland.jpg`: open grassland with a herd.
- `sheet_river_golden.jpg`: a river at golden hour.
- `sheet_overcast.jpg`: a ridge under overcast.

`studio_panel.jpg` is the panel as `capture --ui --shot studio` draws it at 1920x1080. That capture draws the panel over a still backdrop, so the split line there divides nothing. In the game the left side shows the other look.

To make the sheets again: `CRET_SHOT_LOOKS="Default,Richer,Filmic,Overcast,Golden,Crisp" ./build/cretaceous/cretaceous capture DIR --shot river`. A look file's path works in the list too.

## Proof and cost

**Default is today's image.** Each new step in the shader is skipped while its knob is at its default, and each old constant became a setting of the same value.
- The same shots were captured with origin/5.0 and with this branch at Default: river (morning), sunset, dusk, night, rain and ridge, at 1280x720 on the high preset.
- Every pixel matched: the largest difference was 0 of 255 on every channel.

**Cost:** measured at 1920x1080 on high, as the mean of 40 frames per shot, before the plant cards were merged (both sides of the comparison alike). This machine is a CPU renderer (Mesa llvmpipe under Xvfb), so the absolute times are hundreds of times a real GPU's. Only the comparison means something.

| | river frame | herd frame | post pass |
|---|---|---|---|
| origin/5.0 | 1824 ms | 1390 ms | 47.6 ms |
| this branch, Default | 1820 ms | 1384 ms | 46.0 to 46.4 ms |
| this branch, every post knob in use (AgX, toe and shoulder, white balance, lift/gamma/gain, saturation, vibrance, greens, grain, sky and clouds) | 1834 ms | 1386 ms | 65.6 ms |

- At Default nothing costs anything (the differences are noise).
- With every colour knob in use, the final pass costs about 40% more on llvmpipe, which is under 1% of the frame. The hue-based steps (greens, vibrance's skin guard) are the dearest part.
- No knob adds a full-screen pass. Turning **Contact shade** to 0 removes one, the ambient occlusion pass.
- The split compare draws the final pass in two halves, so it costs the same.
