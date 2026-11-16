// Shared presentation.

void stereo_draw_preview(float x0, float y0, float cell)
{
	// 20x15 blocks (each averages 2x2 tiles): cheap enough to draw every frame
	if (fc_smooth < 0.0f)
		return;
	for (int gy = 0; gy < GY; gy += 2)
		for (int gx = 0; gx < GX; gx += 2) {
			float l = (level[gy * GX + gx] + level[gy * GX + gx + 1] +
				level[(gy + 1) * GX + gx] + level[(gy + 1) * GX + gx + 1]) * 0.25f;
			u32 c;
			if (l < -0.05f)
				c = C2D_Color32(255, 160, 40, UI_ALPHA_OPAQUE);      // HUD, in front
			else {
				int v = 255 - (int)(l * 210.0f);        // near bright, back dark
				c = l > 0.95f ? C2D_Color32(20, 30, 90, UI_ALPHA_OPAQUE) : C2D_Color32(v, v, v, UI_ALPHA_OPAQUE);
			}
			C2D_DrawRectSolid(x0 + gx * cell, y0 + gy * cell, 0.5f, cell * 2, cell * 2, c);
		}
}
