cd /proc/device-tree/reserved-memory
for n in *; do [ -d "$n" ] || continue; r=$(od -An -tx1 -v "$n/reg" 2>/dev/null | tr -d ' \n'); s=$(od -An -tx1 -v "$n/size" 2>/dev/null | tr -d ' \n'); c=$(cat "$n/compatible" 2>/dev/null | tr '\0' ' '); echo "$n reg=$r size=$s $c"; done
echo "== memory"; od -An -tx1 -v /proc/device-tree/memory/reg | tr -d '\n'; echo
echo "== splash"; ls /proc/device-tree/soc/ | grep -i -E "mdss|splash" | head
