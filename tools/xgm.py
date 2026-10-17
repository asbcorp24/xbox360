#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, pathlib, shutil, struct, subprocess, wave, zlib
from scriptgen import generate_scripts
from rpggen import generate_rpg, enemy_type_id

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_PROJECT = ROOT / "project" / "game.json"
GAME_HEADER = ROOT / "source" / "generated_game.h"
ASSET_HEADER = ROOT / "source" / "generated_assets.h"
SCRIPT_HEADER = ROOT / "source" / "generated_scripts.h"
RPG_HEADER = ROOT / "source" / "generated_rpg.h"

def load(path):
    try: return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError: raise SystemExit(f"Project not found: {path}")
    except json.JSONDecodeError as e: raise SystemExit(f"Invalid JSON: {e}")

def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")

def pp(a): return pathlib.Path(a.project).resolve() if a.project else DEFAULT_PROJECT
def cfloat(v): return f"{float(v):.4f}f"
def cstr(v): return str(v).replace("\\","\\\\").replace('"','\\"')

def room(d,n):
    if n<0 or n>=len(d["rooms"]): raise SystemExit(f"No room {n}")
    r=d["rooms"][n]
    for k in ("platforms","crystals","tiles","enemies","npcs","triggers","doors","teleports","pickups"):
        r.setdefault(k,[])
    return r

def validate(d):
    e=[]
    if d.get("format") not in (2,3,4,5): e.append("format must be 2, 3, 4 or 5")
    vr=d.get("virtual_resolution")
    if not isinstance(vr,list) or len(vr)!=2 or not all(isinstance(x,int) and x>0 for x in vr): e.append("virtual_resolution must be [w,h]")
    p=d.get("player",{})
    for k in ("start","size"):
        if not isinstance(p.get(k),list) or len(p[k])!=2: e.append(f"player.{k} must be [x,y]")
    for k in ("speed","jump_speed","gravity","max_fall_speed"):
        if not isinstance(p.get(k),(int,float)) or p[k] <= 0: e.append(f"player.{k} must be > 0")
    rooms=d.get("rooms")
    if not isinstance(rooms,list) or not rooms: e.append("rooms must be a non-empty array")
    else:
        for ri,r in enumerate(rooms):
            if not isinstance(r,dict): e.append(f"rooms[{ri}] must be object"); continue
            for k in ("platforms","crystals","tiles","enemies","npcs","triggers","doors","teleports"):
                if not isinstance(r.get(k,[]),list): e.append(f"rooms[{ri}].{k} must be array")
    if "player_sprite" not in d.get("assets",{}): e.append("assets.player_sprite is required")
    return e

def decode_png(path):
    b=path.read_bytes()
    if b[:8] != b"\x89PNG\r\n\x1a\n": raise SystemExit(f"Not PNG: {path}")
    pos=8; w=h=ctype=None; raw=b""
    while pos < len(b):
        n=struct.unpack(">I",b[pos:pos+4])[0]; typ=b[pos+4:pos+8]; dat=b[pos+8:pos+8+n]; pos += 12+n
        if typ==b"IHDR":
            w,h,depth,ctype,comp,filt,inter=struct.unpack(">IIBBBBB",dat)
            if depth!=8 or ctype not in (2,6) or inter!=0: raise SystemExit("PNG must be 8-bit RGB/RGBA, non-interlaced")
        elif typ==b"IDAT": raw += dat
        elif typ==b"IEND": break
    raw=zlib.decompress(raw); bpp=4 if ctype==6 else 3; stride=w*bpp; out=[]; prev=bytearray(stride); p=0
    def paeth(a,b,c):
        q=a+b-c; pa=abs(q-a); pb=abs(q-b); pc=abs(q-c)
        return a if pa<=pb and pa<=pc else b if pb<=pc else c
    for _ in range(h):
        ft=raw[p]; p+=1; scan=bytearray(raw[p:p+stride]); p+=stride
        for x in range(stride):
            a=scan[x-bpp] if x>=bpp else 0; up=prev[x]; ul=prev[x-bpp] if x>=bpp else 0
            if ft==1: scan[x]=(scan[x]+a)&255
            elif ft==2: scan[x]=(scan[x]+up)&255
            elif ft==3: scan[x]=(scan[x]+((a+up)//2))&255
            elif ft==4: scan[x]=(scan[x]+paeth(a,up,ul))&255
            elif ft!=0: raise SystemExit(f"Unsupported PNG filter {ft}")
        for x in range(w):
            i=x*bpp
            out.extend(scan[i:i+4] if ctype==6 else (scan[i],scan[i+1],scan[i+2],255))
        prev=scan
    return w,h,bytes(out)

def read_wav(path):
    with wave.open(str(path),"rb") as w:
        if (w.getnchannels(),w.getsampwidth(),w.getframerate()) != (2,2,48000): raise SystemExit(f"{path}: WAV must be 48kHz signed 16-bit stereo PCM")
        if w.getcomptype()!="NONE": raise SystemExit(f"{path}: WAV must be uncompressed PCM")
        return w.readframes(w.getnframes())

def bytes_c(name,data,per=16):
    rows=["    "+", ".join(f"0x{x:02x}" for x in data[i:i+per]) for i in range(0,len(data),per)]
    return f"static const unsigned char {name}[{len(data)}] = {{\n"+",\n".join(rows)+"\n};\n"

def init_room(r):
    for k in ("platforms","crystals","tiles","enemies","npcs","triggers","doors","teleports","pickups"): r.setdefault(k,[])

def gen_game(d):
    errs=validate(d)
    if errs: raise SystemExit("INVALID:\n  - "+"\n  - ".join(errs))
    vw,vh=d["virtual_resolution"]; p=d["player"]; rooms=d["rooms"]
    for r in rooms: init_room(r)
    plats=[]; crystals=[]; tiles=[]; enemies=[]; npcs=[]; triggers=[]; doors=[]; teleports=[]
    for ri,r in enumerate(rooms):
        plats += [(ri,x) for x in r["platforms"]]
        crystals += [(ri,x) for x in r["crystals"]]
        tiles += [(ri,x) for x in r["tiles"] if int(x.get("tile",0))!=0]
        enemies += [(ri,x) for x in r["enemies"]]
        npcs += [(ri,x) for x in r["npcs"]]
        triggers += [(ri,x) for x in r["triggers"]]
        doors += [(ri,x) for x in r["doors"]]
        teleports += [(ri,x) for x in r["teleports"]]
    ex=d["exit"]
    arr=lambda rows, fallback: ",\n".join(rows) if rows else fallback
    pl=arr([f"    {{ {ri},{cfloat(x['x'])},{cfloat(x['y'])},{cfloat(x['w'])},{cfloat(x['h'])} }}" for ri,x in plats],"    {0,0,0,0,0}")
    cr=arr([f"    {{ {ri},{int(x['x'])},{int(x['y'])},0 }}" for ri,x in crystals],"    {0,-1000,-1000,1}")
    tl=arr([f"    {{ {ri},{int(x['x'])},{int(x['y'])},{int(x.get('tile',1))},{1 if x.get('solid',True) else 0} }}" for ri,x in tiles],"    {0,0,0,0,0}")
    en=arr([f"    {{ {ri},{cfloat(x['x'])},{cfloat(x['y'])},{int(x.get('w',8))},{int(x.get('h',8))},{cfloat(x.get('left',x['x']-24))},{cfloat(x.get('right',x['x']+24))},{cfloat(x.get('speed',0.45))},1,{1 if x.get('spawned',True) else 0},{1 if x.get('spawned',True) else 0},{enemy_type_id(x.get('type','walker'))},{int(x.get('hp',2))},{int(x.get('hp',2))},{int(x.get('damage',1))} }}" for ri,x in enemies],"    {0,-1000,-1000,1,1,0,0,0,0,0,0,1,1,1,1}")
    dialog_lookup={x["id"]:i for i,x in enumerate(d.get("dialogues",[]))}
    np=arr([f'    {{ {ri},{int(x["x"])},{int(x["y"])},{int(x.get("w",8))},{int(x.get("h",12))},"{cstr(x.get("message","..."))}",{dialog_lookup.get(x.get("dialogue"),-1)} }}' for ri,x in npcs],'    {0,-1000,-1000,1,1,"",-1}')
    tr=arr([f'    {{ {ri},{int(x["x"])},{int(x["y"])},{int(x["w"])},{int(x["h"])},{int(x.get("target_room",-1))},{int(x.get("target_x",0))},{int(x.get("target_y",0))},{1 if x.get("once",False) else 0},0,"{cstr(x.get("action","tone"))}" }}' for ri,x in triggers],'    {0,-1000,-1000,1,1,-1,0,0,0,0,""}')
    item_lookup={x["name"]:i for i,x in enumerate(d.get("items",[]))}
    dr=arr([f'    {{ {ri},{int(x["x"])},{int(x["y"])},{int(x.get("w",12))},{int(x.get("h",20))},{int(x["target_room"])},{int(x["target_x"])},{int(x["target_y"])},{1 if x.get("requires_all_crystals",False) else 0},{1 if x.get("locked",False) else 0},{item_lookup.get(x.get("key"),-1)} }}' for ri,x in doors],"    {0,-1000,-1000,1,1,0,0,0,0,0,-1}")
    tp=arr([f"    {{ {ri},{int(x['x'])},{int(x['y'])},{int(x.get('w',12))},{int(x.get('h',12))},{int(x['target_room'])},{int(x['target_x'])},{int(x['target_y'])} }}" for ri,x in teleports],"    {0,-1000,-1000,1,1,0,0,0}")
    name=cstr(d.get("name","Xbox360 Game"))
    def count(n): return max(1,n)
    return f"""/* AUTO-GENERATED by xgm. */
#ifndef GENERATED_GAME_H
#define GENERATED_GAME_H
#define GAME_NAME "{name}"
#define VW {vw}
#define VH {vh}
#define TILE_SIZE {int(d.get("tile_size",16))}
#define ROOM_COUNT {len(rooms)}
#define PLAYER_W {int(p["size"][0])}
#define PLAYER_H {int(p["size"][1])}
#define PLAYER_START_ROOM {int(p.get("start_room",0))}
#define PLAYER_START_X {cfloat(p["start"][0])}
#define PLAYER_START_Y {cfloat(p["start"][1])}
#define PLAYER_SPEED {cfloat(p["speed"])}
#define PLAYER_JUMP_SPEED {cfloat(p["jump_speed"])}
#define GAME_GRAVITY {cfloat(p["gravity"])}
#define GAME_MAX_FALL_SPEED {cfloat(p["max_fall_speed"])}
#define EXIT_ROOM {int(ex["room"])}
#define EXIT_X {int(ex["x"])}
#define EXIT_Y {int(ex["y"])}
#define EXIT_W {int(ex["w"])}
#define EXIT_H {int(ex["h"])}
#define PLATFORM_COUNT {len(plats)}
#define CRYSTAL_COUNT {len(crystals)}
#define TILE_COUNT {len(tiles)}
#define ENEMY_COUNT {len(enemies)}
#define NPC_COUNT {len(npcs)}
#define TRIGGER_COUNT {len(triggers)}
#define DOOR_COUNT {len(doors)}
#define TELEPORT_COUNT {len(teleports)}
static const room_platform_t platforms[{count(len(plats))}]={{\n{pl}\n}};
static room_crystal_t crystals[{count(len(crystals))}]={{\n{cr}\n}};
static const tile_cell_t tiles[{count(len(tiles))}]={{\n{tl}\n}};
static enemy_t enemies[{count(len(enemies))}]={{\n{en}\n}};
static const npc_t npcs[{count(len(npcs))}]={{\n{np}\n}};
static trigger_t triggers[{count(len(triggers))}]={{\n{tr}\n}};
static const door_t doors[{count(len(doors))}]={{\n{dr}\n}};
static const teleport_t teleports[{count(len(teleports))}]={{\n{tp}\n}};
#endif
"""

def gen_assets(d):
    a=d["assets"]; w,h,pix=decode_png((ROOT/a["player_sprite"]).resolve()); fw,fh=a.get("player_frame_size",[8,12])
    if w%fw or h%fh: raise SystemExit("player sprite sheet dimensions must be divisible by frame size")
    chunks=["/* AUTO-GENERATED by xgm. */\n#ifndef GENERATED_ASSETS_H\n#define GENERATED_ASSETS_H\n",
            f"#define PLAYER_SHEET_W {w}\n#define PLAYER_SHEET_H {h}\n#define PLAYER_FRAME_W {fw}\n#define PLAYER_FRAME_H {fh}\n#define PLAYER_FRAME_COUNT {(w//fw)*(h//fh)}\n",
            bytes_c("player_sprite_rgba",pix)]
    sounds=a.get("sounds",{})
    for key in ("jump","collect","hit","door","npc"):
        if key in sounds:
            pcm=read_wav((ROOT/sounds[key]).resolve()); chunks += [f"#define SOUND_{key.upper()}_LEN {len(pcm)}\n",bytes_c(f"sound_{key}_pcm",pcm)]
        else:
            chunks += [f"#define SOUND_{key.upper()}_LEN 0\n",f"static const unsigned char sound_{key}_pcm[1]={{0}};\n"]
    chunks.append("#endif\n"); return "".join(chunks)

def cmd_info(a):
    d=load(pp(a)); print(f"{d.get('name')} | rooms={len(d.get('rooms',[]))} | {d.get('virtual_resolution')}")
    for i,r in enumerate(d["rooms"]):
        init_room(r); print(f"room {i}: platforms={len(r['platforms'])} tiles={len(r['tiles'])} enemies={len(r['enemies'])} npc={len(r['npcs'])} triggers={len(r['triggers'])} doors={len(r['doors'])} teleports={len(r['teleports'])}")

def cmd_list(a):
    d=load(pp(a))
    for i,r in enumerate(d["rooms"]):
        init_room(r); print(f"[room {i}: {r.get('name','')}]")
        for k in ("platforms","crystals","tiles","enemies","npcs","triggers","doors","teleports"):
            for j,x in enumerate(r[k]): print(f"  {k[:-1]} {j}: {x}")

def mutate(a, fn):
    path=pp(a); d=load(path); fn(d); d["format"]=5; save(path,d)

def cmd_add_room(a): mutate(a,lambda d:d["rooms"].append({"name":a.name,"platforms":[{"x":0,"y":160,"w":320,"h":20}],"crystals":[],"tiles":[],"enemies":[],"npcs":[],"triggers":[],"doors":[],"teleports":[]}))
def cmd_add_platform(a): mutate(a,lambda d:room(d,a.room)["platforms"].append({"x":a.x,"y":a.y,"w":a.w,"h":a.h}))
def cmd_add_crystal(a): mutate(a,lambda d:room(d,a.room)["crystals"].append({"x":a.x,"y":a.y}))
def cmd_set_tile(a): mutate(a,lambda d:room(d,a.room)["tiles"].append({"x":a.x,"y":a.y,"tile":a.tile,"solid":not a.decor}))
def cmd_add_enemy(a): mutate(a,lambda d:room(d,a.room)["enemies"].append({"x":a.x,"y":a.y,"w":a.w,"h":a.h,"left":a.left,"right":a.right,"speed":a.speed,"spawned":not a.inactive,"type":a.type,"hp":a.hp,"damage":a.damage}))
def cmd_add_npc(a): mutate(a,lambda d:room(d,a.room)["npcs"].append({"x":a.x,"y":a.y,"w":a.w,"h":a.h,"message":a.message,"dialogue":a.dialogue}))
def cmd_add_trigger(a): mutate(a,lambda d:room(d,a.room)["triggers"].append({"x":a.x,"y":a.y,"w":a.w,"h":a.h,"action":a.action,"target_room":a.target_room,"target_x":a.target_x,"target_y":a.target_y,"once":a.once}))
def cmd_add_door(a): mutate(a,lambda d:room(d,a.room)["doors"].append({"x":a.x,"y":a.y,"w":a.w,"h":a.h,"target_room":a.target_room,"target_x":a.target_x,"target_y":a.target_y,"requires_all_crystals":a.requires_all_crystals,"locked":a.locked,"key":a.key}))
def cmd_add_teleport(a): mutate(a,lambda d:room(d,a.room)["teleports"].append({"x":a.x,"y":a.y,"w":a.w,"h":a.h,"target_room":a.target_room,"target_x":a.target_x,"target_y":a.target_y}))
def cmd_set_player(a): mutate(a,lambda d:(d["player"].update({"start_room":a.room,"start":[a.x,a.y]})))
def cmd_set_exit(a):
    def f(d):
        ex=d.get("exit",{"w":11,"h":21}); d["exit"]={"room":a.room,"x":a.x,"y":a.y,"w":a.w or ex.get("w",11),"h":a.h or ex.get("h",21)}
    mutate(a,f)

def cmd_validate(a):
    errs=validate(load(pp(a)))
    if errs: print("INVALID"); [print(" - "+x) for x in errs]; raise SystemExit(1)
    print("OK")

def cmd_generate(a):
    d=load(pp(a))
    GAME_HEADER.write_text(gen_game(d),encoding="utf-8")
    ASSET_HEADER.write_text(gen_assets(d),encoding="utf-8")
    SCRIPT_HEADER.write_text(generate_scripts(d),encoding="utf-8")
    RPG_HEADER.write_text(generate_rpg(d),encoding="utf-8")
    print(GAME_HEADER); print(ASSET_HEADER); print(SCRIPT_HEADER); print(RPG_HEADER)

def cmd_set_flag(a):
    mutate(a, lambda d: d.setdefault("flags",{}).__setitem__(a.name,a.value))

def cmd_set_var(a):
    mutate(a, lambda d: d.setdefault("variables",{}).__setitem__(a.name,a.value))

def parse_action(spec):
    parts=spec.split(":")
    cmd=parts[0]
    if cmd in ("set_flag","set_var","add_var"):
        if len(parts)!=3: raise SystemExit(f"{cmd} format: {cmd}:name:value")
        return {"cmd":cmd,"name":parts[1],"value":int(parts[2])}
    if cmd in ("open_door","spawn_enemy"):
        if len(parts)!=3: raise SystemExit(f"{cmd} format: {cmd}:room:index")
        return {"cmd":cmd,"room":int(parts[1]),"index":int(parts[2])}
    if cmd=="change_room":
        if len(parts)!=4: raise SystemExit("change_room format: change_room:room:x:y")
        return {"cmd":cmd,"room":int(parts[1]),"x":int(parts[2]),"y":int(parts[3])}
    if cmd=="dialog":
        return {"cmd":"dialog","text":spec[len("dialog:"):] if spec.startswith("dialog:") else ""}
    raise SystemExit(f"Unknown action: {cmd}")

def cmd_add_script(a):
    def f(d):
        cond=None
        if a.if_flag:
            name,val=a.if_flag.split("=",1); cond={"kind":"flag","name":name,"op":"eq","value":int(val)}
        elif a.if_var:
            name,val=a.if_var.split("=",1); cond={"kind":"var","name":name,"op":"eq","value":int(val)}
        d.setdefault("scripts",[]).append({
            "event":a.event,"room":a.room,"target":a.target,"once":a.once,
            "condition":cond,"actions":[parse_action(x) for x in a.do]
        })
    mutate(a,f)


def cmd_set_player_stats(a):
    def f(d):
        if a.health is not None: d["player"]["health"]=a.health
        if a.damage is not None: d["player"]["damage"]=a.damage
    mutate(a,f)

def cmd_add_item(a):
    def f(d):
        items=d.setdefault("items",[])
        if any(x.get("name")==a.name for x in items): raise SystemExit("Item already exists")
        items.append({"name":a.name,"kind":a.kind,"power":a.power,"count":a.count})
    mutate(a,f)

def cmd_add_pickup(a):
    mutate(a,lambda d:room(d,a.room)["pickups"].append({"item":a.item,"x":a.x,"y":a.y,"qty":a.qty}))

def cmd_add_dialog(a):
    def f(d):
        ds=d.setdefault("dialogues",[])
        if any(x.get("id")==a.id for x in ds): raise SystemExit("Dialogue already exists")
        ds.append({"id":a.id,"text":a.text,"choices":[]})
    mutate(a,f)

def cmd_add_choice(a):
    def f(d):
        ds=d.setdefault("dialogues",[])
        node=next((x for x in ds if x.get("id")==a.dialog),None)
        if not node: raise SystemExit("Dialogue not found")
        ch={"text":a.text}
        if a.next: ch["next"]=a.next
        if a.set_flag:
            n,v=a.set_flag.split("=",1); ch["set_flag"]={"name":n,"value":int(v)}
        if a.give_item:
            n,q=a.give_item.split(":",1); ch["give_item"]={"name":n,"qty":int(q)}
        node.setdefault("choices",[]).append(ch)
    mutate(a,f)

def cmd_build(a):
    cmd_generate(a)
    if a.docker:
        if not shutil.which("docker"): raise SystemExit("docker not found")
        cmd=["docker","run","--rm","-v",f"{ROOT.resolve()}:/app","-w","/app","free60/libxenon:latest","sh","-lc","export DEVKITXENON=/usr/local/xenon; export PATH=/usr/local/xenon/bin:/usr/local/xenon/usr/bin:$PATH; make clean && make"]
    else:
        subprocess.check_call(["make","clean"],cwd=ROOT); cmd=["make"]
    subprocess.check_call(cmd,cwd=ROOT)

def parser():
    p=argparse.ArgumentParser(prog="xgm",description="Xbox360 Game Maker CLI"); p.add_argument("--project"); s=p.add_subparsers(dest="cmd",required=True)
    for n,f in [("info",cmd_info),("list",cmd_list),("validate",cmd_validate),("generate",cmd_generate)]: q=s.add_parser(n); q.set_defaults(func=f)
    q=s.add_parser("add-room"); q.add_argument("name"); q.set_defaults(func=cmd_add_room)
    q=s.add_parser("add-platform"); q.add_argument("room",type=int); [q.add_argument(k,type=int) for k in ("x","y","w","h")]; q.set_defaults(func=cmd_add_platform)
    q=s.add_parser("add-crystal"); q.add_argument("room",type=int); q.add_argument("x",type=int); q.add_argument("y",type=int); q.set_defaults(func=cmd_add_crystal)
    q=s.add_parser("set-tile"); q.add_argument("room",type=int); q.add_argument("x",type=int); q.add_argument("y",type=int); q.add_argument("tile",type=int); q.add_argument("--decor",action="store_true"); q.set_defaults(func=cmd_set_tile)
    q=s.add_parser("add-enemy"); q.add_argument("room",type=int); q.add_argument("x",type=int); q.add_argument("y",type=int); q.add_argument("--left",type=float,required=True); q.add_argument("--right",type=float,required=True); q.add_argument("--speed",type=float,default=.45); q.add_argument("--w",type=int,default=8); q.add_argument("--h",type=int,default=8); q.add_argument("--inactive",action="store_true"); q.add_argument("--type",choices=["walker","charger","tank"],default="walker"); q.add_argument("--hp",type=int,default=2); q.add_argument("--damage",type=int,default=1); q.set_defaults(func=cmd_add_enemy)
    q=s.add_parser("add-npc"); q.add_argument("room",type=int); q.add_argument("x",type=int); q.add_argument("y",type=int); q.add_argument("message"); q.add_argument("--w",type=int,default=8); q.add_argument("--h",type=int,default=12); q.add_argument("--dialogue"); q.set_defaults(func=cmd_add_npc)
    q=s.add_parser("add-trigger"); q.add_argument("room",type=int); [q.add_argument(k,type=int) for k in ("x","y","w","h")]; q.add_argument("--action",choices=["tone","checkpoint","teleport"],default="tone"); q.add_argument("--target-room",type=int,default=-1); q.add_argument("--target-x",type=int,default=0); q.add_argument("--target-y",type=int,default=0); q.add_argument("--once",action="store_true"); q.set_defaults(func=cmd_add_trigger)
    q=s.add_parser("add-door"); q.add_argument("room",type=int); q.add_argument("x",type=int); q.add_argument("y",type=int); q.add_argument("target_room",type=int); q.add_argument("target_x",type=int); q.add_argument("target_y",type=int); q.add_argument("--w",type=int,default=12); q.add_argument("--h",type=int,default=20); q.add_argument("--requires-all-crystals",action="store_true"); q.add_argument("--locked",action="store_true"); q.add_argument("--key"); q.set_defaults(func=cmd_add_door)
    q=s.add_parser("add-teleport"); q.add_argument("room",type=int); q.add_argument("x",type=int); q.add_argument("y",type=int); q.add_argument("target_room",type=int); q.add_argument("target_x",type=int); q.add_argument("target_y",type=int); q.add_argument("--w",type=int,default=12); q.add_argument("--h",type=int,default=12); q.set_defaults(func=cmd_add_teleport)
    q=s.add_parser("set-player"); q.add_argument("room",type=int); q.add_argument("x",type=int); q.add_argument("y",type=int); q.set_defaults(func=cmd_set_player)
    q=s.add_parser("set-flag"); q.add_argument("name"); q.add_argument("value",type=int); q.set_defaults(func=cmd_set_flag)
    q=s.add_parser("set-var"); q.add_argument("name"); q.add_argument("value",type=int); q.set_defaults(func=cmd_set_var)
    q=s.add_parser("add-script")
    q.add_argument("event",choices=["on_enter","on_interact","on_collect"])
    q.add_argument("--room",type=int,default=-1); q.add_argument("--target",type=int,default=-1)
    q.add_argument("--if-flag"); q.add_argument("--if-var"); q.add_argument("--once",action="store_true")
    q.add_argument("--do",action="append",required=True,help="Action, e.g. set_flag:key:1, add_var:score:1, open_door:1:0, spawn_enemy:0:0, change_room:2:20:145, dialog:Hello")
    q.set_defaults(func=cmd_add_script)
    q=s.add_parser("set-exit"); q.add_argument("room",type=int); q.add_argument("x",type=int); q.add_argument("y",type=int); q.add_argument("--w",type=int); q.add_argument("--h",type=int); q.set_defaults(func=cmd_set_exit)
    q=s.add_parser("set-player-stats"); q.add_argument("--health",type=int); q.add_argument("--damage",type=int); q.set_defaults(func=cmd_set_player_stats)
    q=s.add_parser("add-item"); q.add_argument("name"); q.add_argument("kind",choices=["key","consumable","weapon"]); q.add_argument("--power",type=int,default=0); q.add_argument("--count",type=int,default=0); q.set_defaults(func=cmd_add_item)
    q=s.add_parser("add-pickup"); q.add_argument("room",type=int); q.add_argument("item"); q.add_argument("x",type=int); q.add_argument("y",type=int); q.add_argument("--qty",type=int,default=1); q.set_defaults(func=cmd_add_pickup)
    q=s.add_parser("add-dialog"); q.add_argument("id"); q.add_argument("text"); q.set_defaults(func=cmd_add_dialog)
    q=s.add_parser("add-choice"); q.add_argument("dialog"); q.add_argument("text"); q.add_argument("--next"); q.add_argument("--set-flag"); q.add_argument("--give-item"); q.set_defaults(func=cmd_add_choice)
    q=s.add_parser("build"); q.add_argument("--docker",action="store_true"); q.set_defaults(func=cmd_build)
    return p

if __name__=="__main__": a=parser().parse_args(); a.func(a)
