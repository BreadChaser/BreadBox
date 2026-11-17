#!/usr/bin/env python3
# Each deliberate break of safefile.c / cards.c must make the host suites fail.
import os, shutil, subprocess, sys, tempfile
root = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../..'))
os.chdir(root)
src=open('app/ui/safefile.c').read()
cards=open('systems/ps1/cards.c').read()
muts={
 'repair tmp by rewrite': ('\t\treturn rename(src, path) == 0;\n\t}\n\tif (from != SAFE_BACKUP)', '\t\treturn write_tmp(src, data, fmt->size, &why) && rename(src, path) == 0;\n\t}\n\tif (from != SAFE_BACKUP)'),
 'stat ignores tmp': ('found = usable(t) && stat(other, &st) == 0;', 'found = false;'),
 'delete damaged': ('return rename(path, bad) == 0;', 'return remove(path) == 0;'),
 'old bad removed': ('\t\tsafe_name_t taken = safe_name(bad, NULL);', '\t\tremove(bad); safe_name_t taken = safe_name(bad, NULL);'),
 'restore unverified': ('\tif (!write_tmp(tmp, data, fmt->size, &why) || !reads_back(tmp, data, fmt->size, &why)) {', '\tif (!write_tmp(tmp, data, fmt->size, &why)) {'),
 'failed publish drops copy': ('\tif (cleanup_present(path))   // also after a cancel (a rollback put <name> back)\n\t\tcleanup_remove(tmp);', '\tcleanup_remove(tmp);'),
 'no rescue': ('\tif (keep_backup && !rescue_tmp(path, tmp, bak, fmt, &why))', '\tif (0 && !rescue_tmp(path, tmp, bak, fmt, &why))'),
 'tolerant size': ('fmt->head>HEAD_MAX || size!=(long)fmt->size ? DAMAGED : GOOD;', 'fmt->head>HEAD_MAX || size<=0 ? DAMAGED : GOOD;'),
 'no head check': ('if(r==GOOD && fmt->valid && fmt->head && !fmt->valid(whole ? whole : head))', 'if(0)'),
 'unchecked close': ('if (fclose_at(f,tmp)){note_failure(why,"close",tmp,errno,0,0);ok=false;}', 'fclose_at(f,tmp);'),
 'rotate damaged': ('\t\tif (cur == GOOD || (cur == WEAK && old != GOOD)) {', '\t\tif (1) {'),
 'weak pushes good backup': ('\t\tif (cur == GOOD || (cur == WEAK && old != GOOD)) {', '\t\tif (usable(cur)) {'),
 'weak preferred': ('\t\tif (r == WEAK && weak == SAFE_NONE)\n\t\t\tweak = s;', '\t\tif (r == WEAK) { info->from = (safe_source_t)s; info->weak = true; return buf; }'),
 'no deep check': ('if(r==GOOD && fmt->deep && !(whole && fmt->deep(whole,fmt->size)))', 'if(0)'),
 'name error as absence': ('\treturn errno == ENOENT ? SAFE_NAME_ABSENT : SAFE_NAME_UNREADABLE;', '\treturn SAFE_NAME_ABSENT;'),
 'open error as absence': ('note_failure(why,"open",path,error,0,0);\n\t\treturn UNREADABLE;', 'return ABSENT;'),
 'read error as damage': ('error=errno;r=UNREADABLE;note_failure', 'error=errno;r=DAMAGED;note_failure'),
 'read close ignored': ('if(!closed){note_failure(why,"close",path,errno,0,0);if(r==GOOD)r=UNREADABLE;}', 'if(!closed){note_failure(why,"close",path,errno,0,0);}'),
 'unreadable tmp truncated': ('\t\treturn t == ABSENT;', '\t\treturn true;'),
 'weak over unreadable': ('\tif (weak != SAFE_NONE && !unreadable) {', '\tif (weak != SAFE_NONE) {'),
 'unreadable said damaged': ('.outcome = unreadable ? SAFE_UNREADABLE : damaged ? SAFE_DAMAGED : SAFE_ABSENT', '.outcome = damaged || unreadable ? SAFE_DAMAGED : SAFE_ABSENT'),
 'replace unseen name': ('\tif (at == SAFE_NAME_UNREADABLE){note_failure(why,"stat",path,errno,0,0);return false;}\n\tif (at == SAFE_NAME_PRESENT) {\n\t\tcopy_t cur', '\tif (at == SAFE_NAME_PRESENT) {\n\t\tcopy_t cur'),
 'unreadable current moved': ('\t\tif (cur == UNREADABLE || old == UNREADABLE)', '\t\tif (old == UNREADABLE)'),
 'weak pushes unread backup': ('\t\tif (cur == UNREADABLE || old == UNREADABLE)', '\t\tif (cur == UNREADABLE)'),
 'tail error accepted': ('(whole && fgetc(f)!=EOF) || ferror(f))', '(whole && fgetc(f)!=EOF))'),
 'readback tail accepted': ('ok=fgetc(f)==EOF && !ferror(f);', 'ok=fgetc(f)==EOF;'),
 'retire unread backup': ('\tif (read_through(bak, why, true) != GOOD)\n\t\treturn false;', ''),
 'park unreadable current': ('if(read_through(path, why, false)==UNREADABLE)return false;', ''),
 'rescue parks unreadable': ('if(cur==UNREADABLE){if(why)*why=current;return false;}\n\tif(!park_readable(path,why))return false;', 'if(!safe_park(path))return false;'),
 'read-through open error as absence': ('if(at==SAFE_NAME_ABSENT && !seen)return ABSENT;\n\t\tnote_failure(why,"open",path,error,0,0);\n\t\tif(error==ENOENT', 'return ABSENT;\n\t\tif(error==ENOENT'),
 'retry arbitrary errors': ('error==ENOENT', '1'),
 'forget observed existence': (' && !seen', ''),
 'ignore shared retry budget': (' || !*why->pauses', ''),
 'skip retry cancel checkpoint': ('#endif\n\tif(!safe_checkpoint()){errno=ECANCELED;return false;}\n\treturn true;', '#endif\n\treturn true;'),
 'retry damage accepted': ('if((contradicted || (why && why->pauses && *why->pauses<2)) && r==DAMAGED)', 'if(false && contradicted && r==DAMAGED)'),
 'successful retry silent': ('if (!why->pauses || *why->pauses == 2)', 'if (true)'),
 'load retry budget dropped': ('read_copy(name, fmt, buf, &why, false)', 'read_copy(name, fmt, buf, NULL, false)'),
}
card_muts={
 'shadow failed repair': ('	srm_dirty = r == CARD_UNSAVED;', '	srm_dirty = false;'),
 'no retry delay': ('	if (srm_dirty && osGetTime() < srm_retry_at)', '	if (0)'),
 'tolerant card': ('	if (!card) {\n		switch (info.outcome)', '	if (!card) {\n		FILE *f = fopen(path, "rb"); if (f) { fread(data, 1, size, f); fclose(f); }\n		switch (info.outcome)'),
 'card 2 in place': ('	str[0] = 0;               // the core', '	(void)0;               // the core'),
 'directory unchecked': ('	for (int f = 0; f < 16; f++) {\n		u8 x = 0;', '	for (int f = 0; f < 1; f++) {\n		u8 x = 0;'),
 'damaged plays on': ('	srm_locked = srm_trouble != CARD_TROUBLE_NONE;\n	srm_dirty = r == CARD_UNSAVED;   // never', '	srm_locked = srm_trouble == CARD_TROUBLE_MEMORY;\n	srm_dirty = r == CARD_UNSAVED;   // never'),
 'no pin before format': ('	if (history_add(h->group, HIST_CARD_PIN, h->ext, old, size))', '	if (0)'),
 'no session copy': ('	history_add(h->group, HIST_CARD_SESSION, h->ext, data, size);', '	(void)0;'),
 'later day copy kept': ('			twin_day |= e->kind == HIST_CARD_DAY && j > i && day_of(all[j].when) == day_of(e->when);', '			twin_day |= e->kind == HIST_CARD_DAY && j < i && day_of(all[j].when) == day_of(e->when);'),
 'ring unbounded': ('			if (++ring > HIST_RING)', '			if (0)'),
 'budget drops the newest': ('				if (!b->e.path[0] || (keep && !strcmp(b->e.path, keep)))', '				if (!b->e.path[0])'),
 'file time in name ignored': ('		e->when = (time_t)when;', '		e->when = 0;'),
 'put back keeps nothing': ('	history_add(t.h->group, HIST_CARD_PIN, t.h->ext, t.image?t.image:t.mem, t.size);   // the card as it is now', '	(void)0;'),
 'card read error is new': ('		case SAFE_ABSENT:\n			return CARD_OK;', '		case SAFE_ABSENT: case SAFE_UNREADABLE:\n			return CARD_OK;'),
 'image kept on new format': ('	if (prefix != c->prefix || (prefix && !c->image)) {', '	if (prefix && !c->image) {'),
 'raw card size unknown': ('	if (at == SAFE_NAME_PRESENT && card_size_known(sz))', '	if (at == SAFE_NAME_PRESENT && card_prefix(sz > 0 ? (size_t)sz : 0))'),
}
tmp = tempfile.mkdtemp(prefix='save-mutations-')
missed = 0
hist=open('app/ui/history.c').read()
def run(name, safefile, cards_src, hist_src=None):
    global missed
    open(tmp+'/safefile.c','w').write(safefile)
    open(tmp+'/cards.c','w').write(cards_src)
    open(tmp+'/history.c','w').write(hist_src or hist)
    shutil.copy('include/breadbox/safefile.h', tmp+'/safefile.h')
    flags=['cc','-std=gnu11','-D_GNU_SOURCE','-w','-g','-DSAFEFILE_TEST','-DHIST_BUDGET=12000','-include','tests/app/save/host.h','-I'+tmp,'-Isystems/ps1','-Itests/app/host','-Iapp/ui','-Iinclude/breadbox','-Iplatform/3ds','-Icore/deps/libretro-common/include']
    caught = False
    wrap=['-Wl,--wrap=stat,--wrap=fopen']   # SD errors on one file (io_errors.c, cards.c)
    for test, extra, link in (('check', [], []), ('io_errors', [], ['-Wl,--wrap=stat,--wrap=fopen,--wrap=nanosleep']),
                              ('cards', [tmp+'/cards.c', tmp+'/history.c','app/ui/bb_final.c'], wrap),
                              ('history', [tmp+'/history.c'], [])):
        r=subprocess.run(flags+['tests/app/save/'+test+'.c',tmp+'/safefile.c',*extra,*link,'-o',tmp+'/t'],capture_output=True,text=True)
        if r.returncode: print(name,'BUILD FAIL',r.stderr[:300]); missed += 1; return
        caught |= subprocess.run([tmp+'/t'],capture_output=True,text=True).returncode != 0
    if name is None:   # the unmutated control: every program must pass
        if caught: sys.exit('unmutated sources fail the save tests; mutations would prove nothing')
        print('unmutated control      passes')
        return
    print(f"{name:24s} {'caught' if caught else 'MISSED'}")
    missed += not caught
run(None, src, cards)
if missed: sys.exit('unmutated control did not build')
for name,(a,b) in card_muts.items():
    if a in hist:   # the backups (history.c)
        run(name, src, cards, hist.replace(a,b))
        continue
    assert a in cards, name
    run(name, src, cards.replace(a,b))
for name,(a,b) in muts.items():
    assert a in src, name
    run(name, src.replace(a,b), cards)
state=open('systems/ps1/statefile.c').read()
state_muts={
 'unbounded read': ('	if (s->refused || len > s->len - s->pos) {\n		s->refused = true;\n		return -1;\n	}\n	memcpy(buf', '	memcpy(buf'),
 'no spu check': ('	if (spu <= SPU_MIN || spu > SPU_MAX || spu > size - SPU_SIZE_AT - sizeof(spu))', '	if (0)'),
 'tail error ignored': ('	if (stream_refused) {   // a record', '	if (0) {   // a record'),
 'negative seek': ('(offs < 0 ? (size_t)-(offs + 1) >= base :', '(offs < 0 ? false :'),
 'tail unchecked': ('	return check_tail(p, size, SPU_SIZE_AT + sizeof(spu) + spu);', '	return STATE_OK;'),
 'pads unchecked': ('		if (saved < PAD_MIN || saved > size - pos)\n			return STATE_DAMAGED;', ''),
 'no crc check': ('	if (t && !crc_ok)\n		return STATE_DAMAGED;', ''),
 'other build layout': ('	if (t && t->tail && t->tail != tail_expected())\n		return STATE_VERSION;', ''),
 'respSize unchecked': ('			if (resp > sizeof(((PadDataS *)0)->rxData) || resp_old > sizeof(((PadDataS *)0)->rxData))\n				return STATE_DAMAGED;', ''),
 'learn from a loaded state': ('	} else\n		// the block list elsewhere: the records before it have other sizes\n		for (size_t i = at; i + sizeof(magic) <= size && i < at + TAIL_SCAN; i++)\n			if (!memcmp(p + i, magic, sizeof(magic)))\n				return STATE_VERSION;', '	} else\n		for (size_t i = at; i + sizeof(magic) <= size && i < at + TAIL_SCAN; i++)\n			if (!memcmp(p + i, magic, sizeof(magic))) {\n				state_tail_measured = i - at;\n				return check_tail(p, size, at);\n			}'),
 'disc index kept patched': ('	if (fixed == 0) {\n		char *p = (char *)data', '	if (0) {\n		char *p = (char *)data'),
 'other disc loads': ('	if (fixed == -2) {\n		state_last_result = STATE_OTHER_DISC;\n		return false;\n	}', ''),
 'hle not restored': ('		Config.HLE = hle;   // the game is restarted', '		(void)hle;   // the game is restarted'),
}
os.makedirs(tmp+'/x/y', exist_ok=True)
os.symlink(os.path.join(root, 'core'), tmp+'/core')
for name,(a,b) in [(None, ('', ''))] + list(state_muts.items()):
    assert a in state, name
    open(tmp+'/x/y/statefile.c','w').write(state.replace(a,b) if name else state)
    r=subprocess.run(['cc','-std=gnu11','-O1','-w','-g','-fsanitize=address,undefined','-Icore/include','-Iapp/ui','-Iinclude/breadbox','-Iplatform/3ds','-Isystems/ps1',
        '-DSTATEFILE_C="'+tmp+'/x/y/statefile.c"','tests/app/save/states.c','-o',tmp+'/s'],capture_output=True,text=True)
    if r.returncode: print(name,'BUILD FAIL',r.stderr[:300]); missed += 1; continue
    caught = subprocess.run([tmp+'/s'],capture_output=True,text=True).returncode != 0
    if name is None:
        if r.returncode or caught: sys.exit('unmutated statefile.c fails states.c; mutations would prove nothing')
        continue
    print(f"{name:24s} {'caught' if caught else 'MISSED'}")
    missed += not caught
shutil.rmtree(tmp)
print(f"mutations: {len(muts)+len(card_muts)+len(state_muts)}, missed {missed}")
sys.exit(missed != 0)
