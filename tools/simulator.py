#!/usr/bin/env python3
from __future__ import annotations

import copy
import json
import pathlib
import sys
import time
import tkinter as tk

ROOT = pathlib.Path(__file__).resolve().parents[1]

class Simulator:
    def __init__(self, project_path: pathlib.Path):
        self.project_path = project_path
        self.data = json.loads(project_path.read_text(encoding="utf-8"))
        self.vw, self.vh = self.data.get("virtual_resolution", [320, 180])
        self.scale = 3
        self.root = tk.Tk()
        self.root.title(f"Xbox360 Game Maker Simulator - {self.data.get('name','Game')}")
        self.root.resizable(False, False)
        self.canvas = tk.Canvas(
            self.root,
            width=self.vw*self.scale,
            height=self.vh*self.scale,
            highlightthickness=0,
            bg="#101428"
        )
        self.canvas.pack()
        self.info = tk.StringVar()
        tk.Label(self.root, textvariable=self.info, anchor="w").pack(fill="x")

        self.keys = set()
        self.just_pressed = set()
        self.root.bind("<KeyPress>", self.on_press)
        self.root.bind("<KeyRelease>", self.on_release)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

        self.images = {}
        self.load_images()
        self.reset()

    def load_images(self):
        paths = set()
        for room in self.data.get("rooms", []):
            vis = room.get("visuals", {})
            for group in ("background","parallax","foreground"):
                layers = vis.get(group, [])
                if isinstance(layers, dict):
                    layers = [layers]
                for layer in layers:
                    if layer.get("image"):
                        paths.add(layer["image"])
        for rel in paths:
            path = ROOT / rel
            try:
                img = tk.PhotoImage(file=str(path))
                if self.scale > 1:
                    img = img.zoom(self.scale, self.scale)
                self.images[rel] = img
            except Exception as e:
                print(f"[sim] PNG load failed: {rel}: {e}")

    def reset(self):
        self.rooms = copy.deepcopy(self.data.get("rooms", []))
        for room in self.rooms:
            for k in ("platforms","crystals","tiles","enemies","npcs","triggers","doors","teleports","pickups","particle_emitters"):
                room.setdefault(k, [])
            for e in room["enemies"]:
                e.setdefault("spawned", True)
                e["_alive"] = bool(e["spawned"])
                e["_hp"] = int(e.get("hp", 2))
                e["_dir"] = 1
            for c in room["crystals"]:
                c["_collected"] = False
            for p in room["pickups"]:
                p["_collected"] = False
            for pe in room["particle_emitters"]:
                pe["_timer"] = 0

        p = self.data.get("player", {})
        self.room = int(p.get("start_room", 0))
        self.x = float(p.get("start", [14,148])[0])
        self.y = float(p.get("start", [14,148])[1])
        self.w = int(p.get("size", [8,12])[0])
        self.h = int(p.get("size", [8,12])[1])
        self.vx = self.vy = 0.0
        self.on_ground = False
        self.facing = 1
        self.max_hp = int(p.get("health", 5))
        self.hp = self.max_hp
        self.invuln = 0
        self.attack_cd = 0
        self.attack_frames = 0
        self.fire_cd = 0
        self.projectiles = []
        self.particles = []
        self.camera_x = 0.0
        self.inventory = {x["name"]: int(x.get("count",0)) for x in self.data.get("items",[])}
        self.flags = copy.deepcopy(self.data.get("flags",{}))
        self.variables = copy.deepcopy(self.data.get("variables",{}))
        self.dialogue = None
        self.dialog_choice = 0
        self.message = ""
        self.script_fired = set()
        self.trigger_fired = set()
        self.open_doors = set()
        self.checkpoint = (self.room,self.x,self.y)
        self.teleport_cooldown = 0
        self.last_room_for_enter = None
        self.running = True
        self.last = time.perf_counter()

    def close(self):
        self.running = False
        self.root.destroy()

    def on_press(self, event):
        key = event.keysym.lower()
        if key not in self.keys:
            self.just_pressed.add(key)
        self.keys.add(key)

    def on_release(self, event):
        self.keys.discard(event.keysym.lower())

    def overlap(self, ax,ay,aw,ah,bx,by,bw,bh):
        return ax < bx+bw and ax+aw > bx and ay < by+bh and ay+ah > by

    def room_data(self):
        return self.rooms[self.room]

    def world_width(self):
        return int(self.room_data().get("world_width", self.vw))

    def solids(self):
        room = self.room_data()
        for p in room["platforms"]:
            yield float(p["x"]),float(p["y"]),float(p["w"]),float(p["h"])
        ts = int(self.data.get("tile_size",16))
        for t in room["tiles"]:
            if t.get("solid",True):
                yield t["x"]*ts,t["y"]*ts,ts,ts

    def collide_vertical(self, old_y):
        self.on_ground = False
        for x,y,w,h in self.solids():
            if not self.overlap(self.x,self.y,self.w,self.h,x,y,w,h):
                continue
            old_bottom = old_y + self.h
            new_bottom = self.y + self.h
            if self.vy >= 0 and old_bottom <= y+1 and new_bottom >= y:
                self.y = y-self.h
                self.vy = 0
                self.on_ground = True
            elif self.vy < 0 and old_y >= y+h-1:
                self.y = y+h
                self.vy = 0

    def damage(self, amount):
        if self.invuln:
            return
        self.hp -= amount
        self.invuln = 45
        if self.hp <= 0:
            self.hp = self.max_hp
            self.respawn()

    def respawn(self):
        self.room,self.x,self.y = self.checkpoint
        self.vx=self.vy=0
        self.camera_x=max(0,self.x-self.vw/2)
        self.teleport_cooldown=20

    def item_power(self, kind):
        best = int(self.data.get("player",{}).get("damage",1))
        for item in self.data.get("items",[]):
            if item.get("kind")==kind and self.inventory.get(item["name"],0)>0:
                best=max(best,int(item.get("power",0)))
        return best

    def attack(self):
        if self.attack_cd:
            return
        self.attack_cd = 20
        self.attack_frames = 6
        ax = self.x+self.w if self.facing>0 else self.x-12
        for e in self.room_data()["enemies"]:
            if not e.get("_alive"):
                continue
            if self.overlap(ax,self.y+2,12,self.h-2,e["x"],e["y"],e.get("w",8),e.get("h",8)):
                e["_hp"] -= self.item_power("weapon")
                if e["_hp"] <= 0:
                    e["_alive"] = False
                else:
                    e["_dir"] *= -1

    def active_gun(self):
        guns=[x for x in self.data.get("items",[]) if x.get("kind")=="ranged" and self.inventory.get(x["name"],0)>0]
        return max(guns,key=lambda x:int(x.get("power",0))) if guns else None

    def particle_burst(self,x,y,kind="spark",count=6,color="#ffb050"):
        import random
        for _ in range(count):
            vx=(random.random()*2-1)*(1.8 if kind=="spark" else .8)
            vy=-(.2+random.random()*(1.5 if kind=="fire" else 1.0))
            gravity=-.005 if kind=="smoke" else (.08 if kind=="spark" else .035)
            self.particles.append({"x":x,"y":y,"vx":vx,"vy":vy,"gravity":gravity,"life":20,"size":2 if kind=="smoke" else 1,"color":color,"room":self.room})

    def shoot(self):
        if self.fire_cd:
            return
        gun=self.active_gun()
        if not gun:
            return
        ammo=gun.get("ammo")
        if not ammo or self.inventory.get(ammo,0)<=0:
            self.message="Out of ammo"
            self.fire_cd=8
            return
        self.inventory[ammo]-=1
        speed=float(gun.get("projectile_speed",5.0))
        self.projectiles.append({
            "x":self.x+(self.w+2 if self.facing>0 else -3),
            "y":self.y+self.h/2,
            "vx":speed*(1 if self.facing>0 else -1),
            "life":int(gun.get("projectile_life",70)),
            "damage":int(gun.get("power",1)),
            "room":self.room
        })
        self.fire_cd=max(1,int(gun.get("fire_rate",10)))
        self.particle_burst(self.projectiles[-1]["x"],self.projectiles[-1]["y"],"fire",5,"#ffd060")

    def point_hits_solid(self,x,y):
        for sx,sy,sw,sh in self.solids():
            if sx<=x<sx+sw and sy<=y<sy+sh:
                return True
        return False

    def update_projectiles(self):
        kept=[]
        for p in self.projectiles:
            if p["room"]!=self.room:
                continue
            p["x"]+=p["vx"]; p["life"]-=1
            if p["life"]<=0 or p["x"]<0 or p["x"]>self.world_width() or self.point_hits_solid(p["x"],p["y"]):
                self.particle_burst(p["x"],p["y"],"spark",4,"#f0b040")
                continue
            hit=False
            for e in self.room_data()["enemies"]:
                if not e.get("_alive"): continue
                if self.overlap(p["x"],p["y"],2,2,e["x"],e["y"],e.get("w",8),e.get("h",8)):
                    e["_hp"]-=p["damage"]; hit=True
                    self.particle_burst(p["x"],p["y"],"spark",7,"#ff9f45")
                    if e["_hp"]<=0:
                        e["_alive"]=False
                        self.particle_burst(e["x"],e["y"],"dust",14,"#c34d55")
                    break
            if not hit: kept.append(p)
        self.projectiles=kept

    def update_particles(self):
        import random
        for e in self.room_data().get("particle_emitters",[]):
            e["_timer"]=e.get("_timer",0)+1
            if e["_timer"]>=max(1,int(e.get("rate",12))):
                e["_timer"]=0
                typ=e.get("type","smoke")
                colors={"smoke":"#b8b8b8","spark":"#ffd060","dust":"#b47c65","fire":"#ff7b32"}
                self.particle_burst(e["x"],e["y"],typ,1,colors.get(typ,"#cccccc"))
        kept=[]
        for p in self.particles:
            if p["room"]!=self.room: continue
            p["x"]+=p["vx"];p["y"]+=p["vy"];p["vy"]+=p["gravity"];p["life"]-=1
            if p["life"]>0: kept.append(p)
        self.particles=kept

    def use_consumable(self):
        for item in self.data.get("items",[]):
            if item.get("kind")=="consumable" and self.inventory.get(item["name"],0)>0:
                self.hp=min(self.max_hp,self.hp+int(item.get("power",1)))
                self.inventory[item["name"]]-=1
                self.message=f"Used {item['name']}"
                return

    def script_condition_ok(self, cond):
        if not cond:
            return True
        kind=cond.get("kind")
        name=cond.get("name","")
        op=cond.get("op","eq")
        rhs=int(cond.get("value",0))
        lhs=int(self.flags.get(name,0) if kind=="flag" else self.variables.get(name,0))
        return {"eq":lhs==rhs,"ne":lhs!=rhs,"gt":lhs>rhs,"lt":lhs<rhs,"ge":lhs>=rhs,"le":lhs<=rhs}.get(op,False)

    def execute_script_action(self, a):
        cmd=a.get("cmd")
        if cmd=="set_flag":
            self.flags[a.get("name","")]=int(a.get("value",0))
        elif cmd=="set_var":
            self.variables[a.get("name","")]=int(a.get("value",0))
        elif cmd=="add_var":
            n=a.get("name",""); self.variables[n]=int(self.variables.get(n,0))+int(a.get("value",0))
        elif cmd=="open_door":
            self.open_doors.add((int(a.get("room",-1)),int(a.get("index",-1))))
        elif cmd=="spawn_enemy":
            rr=int(a.get("room",-1)); idx=int(a.get("index",-1))
            if 0<=rr<len(self.rooms):
                es=self.rooms[rr].get("enemies",[])
                if 0<=idx<len(es): es[idx]["_alive"]=True
        elif cmd=="change_room":
            rr=int(a.get("room",-1))
            if 0<=rr<len(self.rooms):
                self.room=rr; self.x=float(a.get("x",0)); self.y=float(a.get("y",0)); self.vx=self.vy=0; self.camera_x=0
        elif cmd=="dialog":
            self.message=a.get("text","")
        elif cmd=="emit_particles":
            typ=a.get("name","spark")
            colors={"smoke":"#b8b8b8","spark":"#ffd060","dust":"#b47c65","fire":"#ff7b32"}
            self.particle_burst(float(a.get("x",self.x)),float(a.get("y",self.y)),typ,int(a.get("value",8)),colors.get(typ,"#cccccc"))

    def run_event(self, event, target=-1):
        scripts=self.data.get("scripts",[])
        for i,s in enumerate(scripts):
            if s.get("event")!=event: continue
            if int(s.get("room",-1)) not in (-1,self.room): continue
            if int(s.get("target",-1)) not in (-1,target): continue
            if s.get("once") and i in self.script_fired: continue
            if not self.script_condition_ok(s.get("condition")): continue
            if s.get("once"): self.script_fired.add(i)
            for a in s.get("actions",[]): self.execute_script_action(a)

    def process_room_enter(self):
        if self.last_room_for_enter!=self.room:
            self.last_room_for_enter=self.room
            self.run_event("on_enter",-1)

    def process_triggers(self):
        room=self.room_data()
        for i,t in enumerate(room.get("triggers",[])):
            key=(self.room,i)
            if t.get("once") and key in self.trigger_fired: continue
            if not self.overlap(self.x,self.y,self.w,self.h,t["x"],t["y"],t["w"],t["h"]): continue
            if t.get("once"): self.trigger_fired.add(key)
            act=t.get("action","tone")
            if act=="checkpoint":
                self.checkpoint=(self.room,self.x,self.y); self.message="Checkpoint!"
            elif act=="teleport" and int(t.get("target_room",-1))>=0:
                self.room=int(t["target_room"]); self.x=float(t.get("target_x",0)); self.y=float(t.get("target_y",0)); self.vx=self.vy=0; self.camera_x=0
            else:
                self.message="Trigger"

    def process_teleports(self):
        if self.teleport_cooldown>0:
            self.teleport_cooldown-=1; return
        for t in self.room_data().get("teleports",[]):
            if self.overlap(self.x,self.y,self.w,self.h,t["x"],t["y"],t.get("w",12),t.get("h",12)):
                self.room=int(t["target_room"]); self.x=float(t["target_x"]); self.y=float(t["target_y"]); self.vx=self.vy=0; self.camera_x=0; self.teleport_cooldown=30
                return

    def start_dialog(self, dialog_id):
        node = next((x for x in self.data.get("dialogues",[]) if x.get("id")==dialog_id),None)
        if node:
            self.dialogue=node
            self.dialog_choice=0

    def confirm_dialog(self):
        if not self.dialogue:
            return
        choices=self.dialogue.get("choices",[])
        if not choices:
            self.dialogue=None
            return
        ch=choices[self.dialog_choice]
        sf=ch.get("set_flag")
        if sf:
            self.flags[sf.get("name","")]=int(sf.get("value",1))
        gi=ch.get("give_item")
        if gi:
            name=gi.get("name")
            self.inventory[name]=self.inventory.get(name,0)+int(gi.get("qty",1))
        nxt=ch.get("next")
        if nxt:
            self.start_dialog(nxt)
        else:
            self.dialogue=None

    def interact(self):
        room=self.room_data()
        for n in room["npcs"]:
            if self.overlap(self.x-6,self.y-4,self.w+12,self.h+8,n["x"],n["y"],n.get("w",8),n.get("h",12)):
                if n.get("dialogue"):
                    self.start_dialog(n["dialogue"])
                else:
                    self.message=n.get("message","...")
                self.run_event("on_interact", room["npcs"].index(n))
                return
        for di,d in enumerate(room["doors"]):
            if self.overlap(self.x,self.y,self.w,self.h,d["x"]-3,d["y"]-3,d.get("w",12)+6,d.get("h",20)+6):
                key=d.get("key")
                scripted_open=(self.room,di) in self.open_doors
                if key and self.inventory.get(key,0)<=0 and not scripted_open:
                    self.message=f"Need key: {key}"
                    return
                if d.get("locked") and not key and not scripted_open:
                    self.message="Door is locked"
                    return
                self.room=int(d["target_room"])
                self.x=float(d["target_x"]); self.y=float(d["target_y"])
                self.vx=self.vy=0
                self.camera_x=0
                return

    def update_enemies(self):
        for e in self.room_data()["enemies"]:
            if not e.get("_alive"):
                continue
            speed=float(e.get("speed",.45))
            typ=e.get("type","walker")
            if typ=="charger" and abs(self.x-e["x"])<70:
                e["_dir"]=1 if self.x>=e["x"] else -1
                speed*=1.8
            if typ=="tank":
                speed*=.55
            e["x"]+=speed*e["_dir"]
            if e["x"]<=e.get("left",e["x"]-24):
                e["x"]=e.get("left",e["x"]); e["_dir"]=1
            if e["x"]>=e.get("right",e["x"]+24):
                e["x"]=e.get("right",e["x"]); e["_dir"]=-1
            if self.overlap(self.x,self.y,self.w,self.h,e["x"],e["y"],e.get("w",8),e.get("h",8)):
                self.damage(int(e.get("damage",1)))

    def update_pickups(self):
        room=self.room_data()
        for p in room["pickups"]:
            if p.get("_collected"):
                continue
            if self.overlap(self.x,self.y,self.w,self.h,p["x"],p["y"],6,6):
                p["_collected"]=True
                self.inventory[p["item"]]=self.inventory.get(p["item"],0)+int(p.get("qty",1))
                self.message=f"Picked up {p['item']}"
        for ci,c in enumerate(room["crystals"]):
            if c.get("_collected"):
                continue
            if self.overlap(self.x,self.y,self.w,self.h,c["x"],c["y"],6,7):
                c["_collected"]=True
                self.run_event("on_collect",ci)

    def update_camera(self):
        room=self.room_data()
        cam=room.get("camera",{})
        smooth=float(cam.get("smoothing",.18))
        dead=float(cam.get("deadzone",48))
        center=self.camera_x+self.vw/2
        target=self.camera_x
        if self.x<center-dead:
            target=self.x-(self.vw/2-dead)
        elif self.x>center+dead:
            target=self.x-(self.vw/2+dead)
        target=max(0,min(target,max(0,self.world_width()-self.vw)))
        self.camera_x+=(target-self.camera_x)*smooth

    def tick(self):
        if not self.running:
            return
        if "escape" in self.just_pressed:
            self.close(); return

        if self.dialogue:
            choices=self.dialogue.get("choices",[])
            if choices:
                if "up" in self.just_pressed or "w" in self.just_pressed:
                    self.dialog_choice=(self.dialog_choice-1)%len(choices)
                if "down" in self.just_pressed or "s" in self.just_pressed:
                    self.dialog_choice=(self.dialog_choice+1)%len(choices)
            if "return" in self.just_pressed or "space" in self.just_pressed:
                self.confirm_dialog()
        else:
            p=self.data.get("player",{})
            speed=float(p.get("speed",1.25))
            left=("left" in self.keys or "a" in self.keys)
            right=("right" in self.keys or "d" in self.keys)
            if left and not right:
                self.vx=-speed; self.facing=-1
            elif right and not left:
                self.vx=speed; self.facing=1
            else:
                self.vx*=.55
                if abs(self.vx)<.08: self.vx=0

            if ("space" in self.just_pressed or "z" in self.just_pressed) and self.on_ground:
                self.vy=-float(p.get("jump_speed",4.35))
                self.on_ground=False
            if "x" in self.just_pressed:
                self.attack()
            if "c" in self.keys or "control_r" in self.keys:
                self.shoot()
            if "y" in self.just_pressed:
                self.use_consumable()
            if "e" in self.just_pressed or "return" in self.just_pressed:
                self.interact()
            if "r" in self.just_pressed:
                self.reset()

            self.x+=self.vx
            ww=self.world_width()
            if self.x < -self.w:
                if self.room>0:
                    self.room-=1; self.x=self.world_width()-self.w-2; self.camera_x=max(0,self.world_width()-self.vw)
                else: self.x=0
            elif self.x > ww:
                if self.room<len(self.rooms)-1:
                    self.room+=1; self.x=2; self.camera_x=0
                else: self.x=ww-self.w

            old_y=self.y
            self.vy+=float(p.get("gravity",.24))
            self.vy=min(self.vy,float(p.get("max_fall_speed",5)))
            self.y+=self.vy
            self.collide_vertical(old_y)
            if self.y>self.vh+40:
                self.respawn()

            self.update_enemies()
            self.update_projectiles()
            self.update_particles()
            self.update_pickups()
            self.process_triggers()
            self.process_teleports()
            self.process_room_enter()

        if self.invuln: self.invuln-=1
        if self.attack_cd: self.attack_cd-=1
        if self.attack_frames: self.attack_frames-=1
        if self.fire_cd: self.fire_cd-=1
        self.update_camera()
        self.draw()
        self.just_pressed.clear()
        self.root.after(16,self.tick)

    def draw_img_layer(self, layer):
        rel=layer.get("image")
        img=self.images.get(rel)
        if not img:
            return
        iw=img.width()//self.scale
        ih=img.height()//self.scale
        speed=float(layer.get("speed_x",0))
        ox=float(layer.get("offset_x",0))-self.camera_x*speed
        oy=float(layer.get("offset_y",0))
        repeat_x=bool(layer.get("repeat_x",False))
        repeat_y=bool(layer.get("repeat_y",False))
        xs=[ox]
        ys=[oy]
        if repeat_x:
            start=ox
            while start>0: start-=iw
            xs=[]; x=start
            while x<self.vw:
                xs.append(x); x+=iw
        if repeat_y:
            start=oy
            while start>0: start-=ih
            ys=[]; y=start
            while y<self.vh:
                ys.append(y); y+=ih
        for y in ys:
            for x in xs:
                self.canvas.create_image(int(x*self.scale),int(y*self.scale),image=img,anchor="nw")

    def sx(self,x): return (x-self.camera_x)*self.scale
    def sy(self,y): return y*self.scale

    def rect(self,x,y,w,h,fill,outline=""):
        self.canvas.create_rectangle(self.sx(x),self.sy(y),self.sx(x+w),self.sy(y+h),fill=fill,outline=outline)

    def draw(self):
        self.canvas.delete("all")
        room=self.room_data()
        vis=room.get("visuals",{})
        for group in ("background","parallax"):
            layers=vis.get(group,[])
            if isinstance(layers,dict): layers=[layers]
            for layer in sorted(layers,key=lambda x:x.get("order",0)):
                self.draw_img_layer(layer)

        ts=int(self.data.get("tile_size",16))
        for t in room["tiles"]:
            self.rect(t["x"]*ts,t["y"]*ts,ts,ts,"#40485b" if t.get("solid",True) else "#5b4876")
        for p in room["platforms"]:
            self.rect(p["x"],p["y"],p["w"],p["h"],"#373449")
        for c in room["crystals"]:
            if not c.get("_collected"):
                self.rect(c["x"],c["y"],6,7,"#37d7e1")
        for p in room["pickups"]:
            if not p.get("_collected"):
                self.rect(p["x"],p["y"],6,6,"#e6be46")
        for n in room["npcs"]:
            self.rect(n["x"],n["y"],n.get("w",8),n.get("h",12),"#4f9cbe")
        for d in room["doors"]:
            self.rect(d["x"],d["y"],d.get("w",12),d.get("h",20),"#615074")
        for e in room["enemies"]:
            if e.get("_alive"):
                color={"walker":"#ee5d5d","charger":"#f58b45","tank":"#9d496f"}.get(e.get("type"),"#ee5d5d")
                self.rect(e["x"],e["y"],e.get("w",8),e.get("h",8),color)
        for p in self.projectiles:
            if p["room"]==self.room: self.rect(p["x"],p["y"],3,1,"#ffe078")
        for p in self.particles:
            if p["room"]==self.room: self.rect(p["x"],p["y"],p.get("size",1),p.get("size",1),p.get("color","#cccccc"))

        player_color="#ffc489" if not self.invuln or (self.invuln//4)%2==0 else "#ffffff"
        self.rect(self.x,self.y,self.w,self.h,player_color)
        if self.attack_frames:
            ax=self.x+self.w if self.facing>0 else self.x-12
            self.rect(ax,self.y+2,12,self.h-2,"#f5e6b4")

        layers=vis.get("foreground",[])
        if isinstance(layers,dict): layers=[layers]
        for layer in sorted(layers,key=lambda x:x.get("order",0)):
            self.draw_img_layer(layer)

        # screen-fixed HUD
        self.canvas.create_rectangle(12,12,150,30,fill="#15152a",outline="")
        hpw=int(120*self.hp/max(1,self.max_hp))
        self.canvas.create_rectangle(18,18,18+hpw,24,fill="#dc4848",outline="")
        inv=", ".join(f"{k}:{v}" for k,v in self.inventory.items() if v>0)
        self.canvas.create_text(12,36,anchor="nw",fill="white",text=f"Room {self.room}  HP {self.hp}/{self.max_hp}  {inv}",font=("Consolas",10))
        self.canvas.create_text(12,self.vh*self.scale-22,anchor="nw",fill="#d0d0d0",text="A/D move  SPACE jump  X melee  C/RightCtrl shoot  Y item  E interact  R reset  ESC quit",font=("Consolas",9))

        if self.message:
            self.canvas.create_rectangle(120,60,840,120,fill="#f5f0dc",outline="#302c3e")
            self.canvas.create_text(135,75,anchor="nw",fill="#302c3e",text=self.message,font=("Consolas",11))

        if self.dialogue:
            self.canvas.create_rectangle(90,45,870,195,fill="#f5f0dc",outline="#302c3e")
            self.canvas.create_text(110,60,anchor="nw",width=730,fill="#302c3e",text=self.dialogue.get("text",""),font=("Consolas",12))
            y=100
            for i,ch in enumerate(self.dialogue.get("choices",[])):
                prefix="> " if i==self.dialog_choice else "  "
                self.canvas.create_text(110,y,anchor="nw",fill="#8e263d" if i==self.dialog_choice else "#302c3e",text=prefix+ch.get("text",""),font=("Consolas",11))
                y+=25

        self.info.set(f"camera={self.camera_x:.1f}  player=({self.x:.1f},{self.y:.1f})  room_width={self.world_width()}")

    def run(self):
        self.root.after(0,self.tick)
        self.root.mainloop()

def main(project=None):
    path=pathlib.Path(project).resolve() if project else ROOT/"project"/"game.json"
    Simulator(path).run()

if __name__=="__main__":
    main(sys.argv[1] if len(sys.argv)>1 else None)
