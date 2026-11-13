// Shared presentation.

void stereo_draw_preview(float x0, float y0, float cell)
{
	// 20x15 blocks (each averages 2x2 tiles): cheap enough to draw every frame
	if (fc_smooth < UI_METRIC_0_0F)
		return;
	for (int gy = UI_METRIC_0; gy < GY; gy += UI_METRIC_2)
		for (int gx = UI_METRIC_0; gx < GX; gx += UI_METRIC_2) {
			float l = (level[gy * GX + gx] + level[gy * GX + gx + UI_METRIC_1] +
				level[(gy + UI_METRIC_1) * GX + gx] + level[(gy + UI_METRIC_1) * GX + gx + UI_METRIC_1]) * UI_METRIC_0_25F;
			u32 c;
			if (l < -UI_METRIC_0_05F)
				c = C2D_Color32(UI_METRIC_255, UI_METRIC_160, UI_METRIC_40, UI_METRIC_255);      // HUD, in front
			else {
				int v = UI_METRIC_255 - (int)(l * UI_METRIC_210_0F);        // near bright, back dark
				c = l > UI_METRIC_0_95F ? C2D_Color32(UI_METRIC_20, UI_METRIC_30, UI_METRIC_90, UI_METRIC_255) : C2D_Color32(v, v, v, UI_METRIC_255);
			}
			C2D_DrawRectSolid(x0 + gx * cell, y0 + gy * cell, UI_METRIC_0_5F, cell * UI_METRIC_2, cell * UI_METRIC_2, c);
		}
}
