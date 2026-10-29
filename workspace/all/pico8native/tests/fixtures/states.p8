pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- save state check: a busy Lua heap (tables and strings made and dropped every
-- frame), rnd, music and a frame count, so a state put back has to bring all of them
p={} t=0 log={}
function _init() music(0) end
function _update60()
 t+=1
 if #p<1500 then for i=1,8 do add(p,{x=rnd(128),y=rnd(128),dx=rnd(2)-1,dy=rnd(2)-1,c=1+flr(rnd(15))}) end end
 for q in all(p) do
  q.x+=q.dx q.y+=q.dy
  if q.x<0 or q.x>127 or q.y<0 or q.y>127 then del(p,q) end
 end
 add(log,"f"..t..":"..#p) if #log>200 then deli(log,1) end
end
function _draw()
 cls(0)
 for q in all(p) do pset(q.x,q.y,q.c) end
 print(t.." "..#p.." "..log[#log],1,1,7)
end
__sfx__
000800001805018050240502405018050180502405024050000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000c0500e0501005011050130501505017050180501a0501c0501d0501f05021050230502405026050000000000000000000000000000000000000000000000000000000000000000000000000000000000
__music__
03 00014344
