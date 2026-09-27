"""Rules agent: top-replay opening + hands place animals + farmer maintenance loop."""
ANIMAL_TILE = ("COOP", "PASTURE")
SHED_CELLS = ((4,4),(5,4),(4,5),(5,5))

def agent(obs):
    step = obs["step"]
    me = obs["farms"][obs["player"]]
    private = obs["private"]
    day = obs["day"]
    tiles = me["tiles"]
    fx, fy = me["farmer"]
    shed = private.get("shed", {})
    seeds = private.get("seeds", {})
    hand_pos = me.get("hands", [])
    hands = [["PASS"] for _ in hand_pos]
    market = []

    if step == 0:
        return {"farmer": ["PASS"], "hands": [],
                "market": [["HIRE"],["HIRE"],["HIRE"],["HIRE"],["HIRE"],
                           ["BUY_ANIMAL","COW",2],["BUY_ANIMAL","SHEEP",2],
                           ["BUY_SEED","WHEAT",7],["BUY_SEED","MELON",10],
                           ["BUY_PRODUCT","WHEAT",5]]}

    def near_shed(x,y):
        return (x,y) in SHED_CELLS
    def animals_placed():
        return sum(1 for row in tiles for t in row if isinstance(t,dict) and t.get("kind") in ANIMAL_TILE and t.get("animal"))
    def empty_tiles():
        out=[]
        for y in range(5):
            for x in range(5):
                if tiles[y][x] is None: out.append((x,y))
        return out

    # ---- farmer: if animals placed, maintenance loop; else help build/place ----
    farmer = ["PASS"]
    tile = tiles[fy][fx]
    placed = animals_placed()
    if placed >= 2:
        if isinstance(tile, dict) and tile.get("kind") in ANIMAL_TILE and tile.get("animal"):
            if tile.get("fertilizer_available"):
                farmer = ["COLLECT_FERTILIZER"]
            elif tile.get("yield_units",0) > 0:
                farmer = ["HARVEST"]
            elif not tile.get("fed_today"):
                farmer = ["FEED", "WHEAT"]
            elif not tile.get("cared_today"):
                farmer = ["CARE"]
        else:
            # walk to an animal needing attention
            target=None; bd=99
            for y in range(10):
                for x in range(10):
                    t=tiles[y][x]
                    if isinstance(t,dict) and t.get("kind") in ANIMAL_TILE and t.get("animal"):
                        need = (not t.get("fed_today")) or t.get("fertilizer_available") or (t.get("yield_units",0)>0) or (not t.get("cared_today"))
                        if need:
                            d=abs(x-fx)+abs(y-fy)
                            if d<bd: bd=d; target=(x,y)
            if target:
                x,y=target
                if y<fy: farmer=["NORTH"]
                elif y>fy: farmer=["SOUTH"]
                elif x<fx: farmer=["WEST"]
                elif x>fx: farmer=["EAST"]
            elif near_shed(fx,fy):
                farmer=["PICKUP","WHEAT",1]
            else:
                if fy>5: farmer=["NORTH"]
                elif fy<4: farmer=["SOUTH"]
                elif fx>5: farmer=["WEST"]
                elif fx<4: farmer=["EAST"]
    else:
        # build/place phase: farmer builds pastures near shed
        empty = empty_tiles()
        if isinstance(tile, dict) and tile.get("kind") in ANIMAL_TILE and not tile.get("animal"):
            # we're on an empty structure; hand will place. move off / build elsewhere
            farmer=["PASS"]
        elif empty:
            # build at the closest empty NW tile
            x,y = min(empty, key=lambda p: abs(p[0]-fx)+abs(p[1]-fy))
            if (x,y)==(fx,fy):
                farmer=["BUILD_PASTURE"]
            else:
                if y<fy: farmer=["NORTH"]
                elif y>fy: farmer=["SOUTH"]
                elif x<fx: farmer=["WEST"]
                elif x>fx: farmer=["EAST"]
        else:
            farmer=["PASS"]

    # ---- hands: first 2 place animals (pickup at shed -> move -> place) ----
    for hi in range(len(hands)):
        if hi >= 2: break
        if hi >= len(hand_pos): break
        hx, hy = hand_pos[hi]
        ht = tiles[hy][hx]
        inv = private["inventories"][hi] if hi < len(private["inventories"]) else {}
        # target an empty pasture
        target = None
        for y in range(10):
            for x in range(10):
                t = tiles[y][x]
                if isinstance(t,dict) and t.get("kind")=="PASTURE" and not t.get("animal"):
                    target=(x,y); break
            if target: break
        if target:
            x,y = target
            item = "SHEEP" if hi==0 else "COW"
            if inv.get(item,0) > 0:
                if (hx,hy)==(x,y):
                    hands[hi] = ["PLACE", item]
                else:
                    if y<hy: hands[hi]=["NORTH"]
                    elif y>hy: hands[hi]=["SOUTH"]
                    elif x<hx: hands[hi]=["WEST"]
                    elif x>hx: hands[hi]=["EAST"]
            elif shed.get(item,0) > 0 and near_shed(hx,hy):
                hands[hi] = ["PICKUP", item, 1]
            elif near_shed(hx,hy):
                hands[hi] = ["PASS"]
            else:
                # go to shed to pick up
                if hy>5: hands[hi]=["NORTH"]
                elif hy<4: hands[hi]=["SOUTH"]
                elif hx>5: hands[hi]=["WEST"]
                elif hx<4: hands[hi]=["EAST"]
        elif isinstance(ht, dict) and ht.get("kind")=="PLANT":
            if not ht.get("watered_today"): hands[hi]=["WATER"]
            elif ht.get("yield_units",0)>0 and day>=2: hands[hi]=["HARVEST"]
        elif ht is None and seeds.get("WHEAT",0)>0:
            hands[hi]=["PLANT","WHEAT"]

    # remaining hands: crop work
    for hi in range(2, len(hands)):
        if hi >= len(hand_pos): break
        hx, hy = hand_pos[hi]
        ht = tiles[hy][hx]
        if isinstance(ht, dict) and ht.get("kind")=="PLANT":
            if not ht.get("watered_today"): hands[hi]=["WATER"]
            elif ht.get("yield_units",0)>0 and day>=2: hands[hi]=["HARVEST"]
        elif ht is None and seeds.get("WHEAT",0)>0:
            hands[hi]=["PLANT","WHEAT"]

    # market: sell produce (keep wheat reserve), buy wheat if low
    for item in ("MELON","CARROT","TOMATO","STRAWBERRY","EGG","MILK","WOOL","FERTILIZER"):
        amt = shed.get(item,0)
        if amt > 0: market.append(["SELL", item, amt])
    wheat = shed.get("WHEAT",0)
    if wheat > 15: market.append(["SELL","WHEAT", wheat-15])
    elif wheat < 8 and me["money"] > 50: market.append(["BUY_PRODUCT","WHEAT",6])
    if day >= 1 and len(hand_pos) < 4 and me["money"] > 100: market.append(["HIRE"])
    return {"farmer": farmer, "hands": hands, "market": market[:10]}
