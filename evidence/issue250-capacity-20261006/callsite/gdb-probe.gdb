start
set pagination off
set confirm off
set print frame-arguments none
set debuginfod enabled off
set $seen = 0
break *pgraph_vk_finish if $esi == 11
commands
silent
set $seen = $seen + 1
printf "FOLDED_READBACK_STACK %d\n", $seen
bt 12
if $seen >= 4
  quit
end
continue
end
continue
