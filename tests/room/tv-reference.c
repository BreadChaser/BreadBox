// Frozen pixel oracle from a5337fd8; compiled only inside tv.py with fake GPU.
static void tile16(u16 *dst, const u16 *src, int w, int h)
{
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			u32 i = (((y >> 3) * (w >> 3) + (x >> 3)) << 6) |
				(x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
			dst[i] = src[y * w + x];
		}
}

static bool tv_maps(void)
{
	if (tv_map) return true;
	tv_map = malloc(256 * 192 * 2);
	tv_k = malloc(256 * 192);
	if (!tv_map || !tv_k) { free(tv_map); free(tv_k); tv_map = NULL; tv_k = NULL; return false; }
	for (int y = 0; y < 192; y++)
		for (int x = 0; x < 256; x++) {
			float u = (x + .5f) / 128 - 1, v = (y + .5f) / 96 - 1;
			float cu = u * (1 + .07f * v * v), cv = v * (1 + .09f * u * u);   // barrel: the curved glass
			int i = y * 256 + x;
			if (fabsf(cu) > 1 || fabsf(cv) > 1) tv_map[i] = 0xffff;
			else {
				int sx = (int)((cu + 1) * 128), sy = (int)((cv + 1) * 96);
				tv_map[i] = (u16)((sy > 191 ? 191 : sy) * 256 + (sx > 255 ? 255 : sx));
			}
			float k = (y & 1 ? .8f : 1) * (1 - .32f * (u * u + v * v) * .5f);   // scanlines, falloff
			tv_k[i] = (u8)(k * 255);
		}
	return true;
}

static u16 rgb565i(int r, int g, int b)   // 0..255 each
{
	r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
	return (u16)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}

void room_scene_tv(const u16 *px)
{
	if (!S.ok || !tv_maps()) return;
	if (!S.tv.data && !C3D_TexInit(&S.tv, 256, 256, GPU_RGB565)) return;
	u16 *out = malloc(256 * 256 * 2);
	if (!out) return;
	memset(out, 0, 256 * 256 * 2);
	S.tv_static = !px;
	uint32_t seed = 0x2545f491u;
	for (int i = 0; i < 256 * 192; i++) {
		int k = tv_k[i], r, g, b;
		if (px) {
			u16 m = tv_map[i];
			if (m == 0xffff) continue;
			u16 c = px[m];
			r = (c >> 11) * 527 >> 6; g = ((c >> 5) & 63) * 259 >> 6; b = (c & 31) * 527 >> 6;   // to 0..255
			int avg = (r + g + b) / 3, glow = avg > 140 ? (avg - 140) * 57 >> 8 : 0;          // the bright parts bloom a little
			r = (r * 271 >> 8) + glow; g = (g * 271 >> 8) + glow; b = (b * 271 >> 8) + glow;
		} else {   // snow: a wrapping noise tile, moved per frame by the uv offset
			seed = seed * 1664525u + 1013904223u;
			r = g = b = 31 + (int)(140 * (seed >> 24) >> 8);
		}
		out[i] = rgb565i(r * k >> 8, g * k >> 8, (b * k >> 8) * 264 >> 8);
	}
	for (int i = 0; i < 64 * 256 && !px; i++) out[192 * 256 + i] = out[i];   // the snow wraps vertically too
	tile16(S.tv.data, out, 256, 256);
	free(out);
	C3D_TexFlush(&S.tv);
	C3D_TexSetFilter(&S.tv, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&S.tv, GPU_REPEAT, GPU_REPEAT);
	S.tv_ok = true;
}

void room_scene_tv_tex(C3D_Tex *t, const Tex3DS_SubTexture *sub)
{
	if (!t || !sub || !t->data || t->fmt != GPU_RGB565) { room_scene_tv(NULL); return; }
	u16 *px = malloc(256 * 192 * 2);
	if (!px) return;
	const u16 *d = t->data;
	// aspect kept: a portrait cover gets black sides, a 4:3 picture fills the screen
	float aspect = sub->height ? (float)sub->width / sub->height : 4 / 3.0f, w = fminf(256, 192 * aspect), x0 = (256 - w) / 2;
	for (int y = 0; y < 192; y++)
		for (int x = 0; x < 256; x++) {
			if (x < x0 || x >= x0 + w) { px[y * 256 + x] = 0; continue; }
			float tu = sub->left + (sub->right - sub->left) * (x - x0 + .5f) / w, tv = sub->top + (sub->bottom - sub->top) * (y + .5f) / 192;
			int sx = (int)(tu * t->width), sy = (int)((1 - tv) * t->height);
			sx = sx < 0 ? 0 : sx >= t->width ? t->width - 1 : sx;
			sy = sy < 0 ? 0 : sy >= t->height ? t->height - 1 : sy;
			px[y * 256 + x] = d[(((sy >> 3) * (t->width >> 3) + (sx >> 3)) << 6) | (sx & 1) | ((sy & 1) << 1) | ((sx & 2) << 1) | ((sy & 2) << 2) | ((sx & 4) << 2) | ((sy & 4) << 3)];
		}
	room_scene_tv(px);
	free(px);
}
