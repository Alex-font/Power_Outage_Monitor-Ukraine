#!/usr/bin/env python3
"""Генератор places_data.h з файлу ukr-populated-places.xlsx (COD-AB, HDX).

Використання:   python gen_places.py ukr-populated-places.xlsx ../places_data.h
Потрібна бібліотека openpyxl (pip install openpyxl).

Що робить:
  * групує населені пункти: область -> район -> населений пункт (громаду зберігає окремим довідником);
  * сортує за українським алфавітом (А Б В Г Ґ Д Е Є Ж З И І Ї Й К ...);
  * назви населених пунктів пише в CP1251 (1 байт на літеру), щоб вмістилось у флеш-пам'ять.
Формат запису населеного пункту в масиві PLACES:
  байт1 байт2 назва... 0   де (байт1-1) + 255*(байт2-1) = номер громади в HROMS.
"""
import sys, collections
import openpyxl

ALPHA = "абвгґдеєжзиіїйклмнопрстуфхцчшщьюя"
def ukey(s):
    out = []
    for ch in s.lower():
        if ch in ALPHA: out.append(10 + ALPHA.index(ch))
        elif ch == "'": out.append(1)
        elif ch == "-": out.append(2)
        elif ch == " ": out.append(3)
        else: out.append(100 + ord(ch) % 100)
    return out

def cesc(b):
    """Байт -> фрагмент C-рядка (вісімкові екрани завжди по 3 цифри, тому злиття з цифрами неможливе)."""
    if 0x20 <= b < 0x7F and chr(b) not in '"\\?':
        return chr(b)
    return "\\%03o" % b

def cstr_utf8(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

def main(xlsx, out):
    wb = openpyxl.load_workbook(xlsx, read_only=True)
    ws = wb["Populated Places"]
    rows = list(ws.iter_rows(values_only=True))
    ix = {k: i for i, k in enumerate(rows[0])}
    R = rows[1:]

    def g(r, k): return r[ix[k]]

    # --- області ---
    def obl_display(code, name):
        if code == "UA01": return "АР Крим", "Крим"
        if code in ("UA80", "UA85"): return "м. " + name, name
        return name + " обл.", name
    obls = {}
    for r in R:
        code, name = g(r, "adm1_pcode"), g(r, "adm1_name1")
        if code not in obls:
            obls[code] = obl_display(code, name)
    obl_order = sorted(obls, key=lambda c: ukey(obls[c][1]))

    # --- громади ---
    hrom_names = collections.OrderedDict()
    for r in sorted(R, key=lambda r: (g(r, "adm3_pcode"))):
        hrom_names.setdefault(g(r, "adm3_pcode"), g(r, "adm3_name1"))
    hrom_idx = {c: i for i, c in enumerate(hrom_names)}
    assert len(hrom_idx) < 255 * 255

    # --- райони та населені пункти ---
    by_raion = collections.defaultdict(list)
    raion_name = {}
    raion_obl = {}
    for r in R:
        rc = g(r, "adm2_pcode")
        by_raion[rc].append(r)
        raion_name[rc] = g(r, "adm2_name1")
        raion_obl[rc] = g(r, "adm1_pcode")

    places = bytearray()
    raion_rows = []   # (name, offset, count)
    obl_rows = []     # (pcode, display, raion0, nRaion)
    for oc in obl_order:
        rcs = sorted([rc for rc in raion_name if raion_obl[rc] == oc], key=lambda rc: ukey(raion_name[rc]))
        obl_rows.append((oc, obls[oc][0], len(raion_rows), len(rcs)))
        for rc in rcs:
            recs = sorted(by_raion[rc], key=lambda r: (ukey(g(r, "adm4_name1")), g(r, "adm3_name1"), g(r, "adm4_pcode")))
            off = len(places)
            for r in recs:
                h = hrom_idx[g(r, "adm3_pcode")]
                places.append((h % 255) + 1)
                places.append((h // 255) + 1)
                places += g(r, "adm4_name1").encode("cp1251")
                places.append(0)
            raion_rows.append((raion_name[rc], off, len(recs)))
    assert len(obl_rows) < 256 and len(raion_rows) < 256

    with open(out, "w", encoding="utf-8") as f:
        f.write("#pragma once\n")
        f.write("// ЗГЕНЕРОВАНО tools/gen_places.py з ukr-populated-places.xlsx (COD-AB Україна, станом на 10.06.2026). НЕ РЕДАГУВАТИ ВРУЧНУ.\n")
        f.write("// Населених пунктів: %d, районів: %d, громад: %d, розмір назв: %d байт (CP1251).\n" % (len(R), len(raion_rows), len(hrom_names), len(places)))
        f.write("#include <stdint.h>\n\n")
        f.write("struct OblInfo { const char* pcode; const char* name; uint8_t raion0; uint8_t nRaion; };\n")
        f.write("struct RaionInfo { const char* name; uint32_t off; uint16_t count; };\n\n")
        f.write("constexpr int OBL_COUNT = %d;\nconstexpr int RAION_COUNT = %d;\nconstexpr int HROM_COUNT = %d;\n\n" % (len(obl_rows), len(raion_rows), len(hrom_names)))
        f.write("static const OblInfo OBLS[OBL_COUNT] = {\n")
        for pc, nm, r0, n in obl_rows:
            f.write("  {%s, %s, %d, %d},\n" % (cstr_utf8(pc), cstr_utf8(nm), r0, n))
        f.write("};\n\n")
        f.write("static const RaionInfo RAIONS[RAION_COUNT] = {\n")
        for nm, off, n in raion_rows:
            f.write("  {%s, %d, %d},\n" % (cstr_utf8(nm), off, n))
        f.write("};\n\n")
        f.write("static const char* const HROMS[HROM_COUNT] = {\n")
        for nm in hrom_names.values():
            f.write("  %s,\n" % cstr_utf8(nm))
        f.write("};\n\n")
        f.write("// Записи: [громада lo+1][громада hi+1][назва CP1251]\\0, по районах підряд.\n")
        f.write("static const char PLACES[] =\n")
        pos = 0
        while pos < len(places):
            end = places.index(0, pos + 2) + 1
            f.write('  "' + "".join(cesc(b) for b in places[pos:end - 1]) + '\\000"\n')
            pos = end
        f.write(";\n")
    print("OK:", len(R), "places,", len(raion_rows), "raions,", len(places), "bytes")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
