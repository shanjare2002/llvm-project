

class land_water:
    
    def __init__(self):
        self.water = set()
        self.land = set()
    
    def add_land(self, x, y):
        self.land.add((x, y))
    def add_water(self, x, y):
        self.water.add((x, y))
    def isLand(self, x, y):
        if (x,y) in self.land:
            return True
        return False
    def isWater(self, x, y):
        if (x,y) in self.water:
            return True
        return False
    def num_islands(self):
        visited = set()
        island = 0
        def dfs(x,y):
            if is_land(x, y):
                visited.add(x, y)
                dfs(x + 1, y)
                dfs(x - 1, y)
                dfs(x, y + 1)
                dfs(x, y - 1)

        for x, y in self.land:
            if (x,y) not in visited:
                dfs(x, y)
                island += 1
        return island
    
# create object

# reset
lw = land_water()

# ---- Test Case 2 ----
lw.add_land(0, 0)
lw.add_land(1, 0)
print("Test 2 done")
print(lw.num_islands())
# reset
lw = land_water()

# ---- Test Case 3 ----
lw.add_land(0, 0)
lw.add_land(0, 1)
print("Test 3 done")

# reset
lw = land_water()

# ---- Test Case 4 ----
lw.add_land(0, 0)
lw.add_land(1, 1)
print("Test 4 done")

# reset
lw = land_water()

# ---- Test Case 5 ----
lw.add_land(0, 0)
lw.add_land(1, 0)
lw.add_land(2, 0)
print("Test 5 done")

# reset
lw = land_water()

# ---- Test Case 6 ----
lw.add_land(0, 0)
lw.add_land(1, 0)
lw.add_land(1, 1)
print("Test 6 done")

# reset
lw = land_water()

# ---- Test Case 7 ----
lw.add_land(0, 0)
lw.add_land(0, 1)
lw.add_land(1, 0)
lw.add_land(1, 1)
print("Test 7 done")

# reset
lw = land_water()

# ---- Test Case 8 ----
lw.add_land(0, 0)
lw.add_land(5, 5)
print("Test 8 done")

# reset
lw = land_water()

# ---- Test Case 9 ----
lw.add_land(0, 0)
lw.add_land(0, 1)
lw.add_land(5, 5)
lw.add_land(6, 5)
print("Test 9 done")

# reset
lw = land_water()

# ---- Test Case 10 ----
lw.add_land(0, 0)
lw.add_land(2, 2)
lw.add_land(4, 4)
print("Test 10 done")

# reset
lw = land_water()

# ---- Test Case 11 ----
lw.add_land(1, 0)
lw.add_land(0, 1)
lw.add_land(1, 1)

