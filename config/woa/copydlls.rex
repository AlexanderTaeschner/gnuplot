ARMLIBS = "/c/msys64/clangarm64/bin/"
ARMLIBS = "C:\msys64\clangarm64\bin\"
parse arg DEST
DEST = translate(DEST, "\", "/")
if DEST = "" then DEST = ".\gnuplot\bin"
say DEST

"rxqueue /clear"

executables = "gnuplot.exe gnuplot_qt.exe"
dlllist.0 = 0
prevdllcount = 0

do w = 1 to words(executables)
  call findlls word(executables, w), 0
  dllcount = dlllist.0

  iter = 0
  do while (prevdllcount \= dllcount)
    -- list all found DLLs
    do i = prevdllcount + 1 to dllcount
      say copies(" ", iter) || dlllist.i
    end
    -- search new DLLs
    do i = prevdllcount +1 to dllcount
      call findlls dlllist.i, 1
    end
    -- iterate
    prevdllcount = dllcount
    dllcount = dlllist.0
    iter = iter + 1
  end
end

-- copy all files in the list
do i = 1 to dlllist.0
  say dlllist.i
  "copy" '"'dlllist.i'"' DEST
end

return

findlls: procedure expose ARMLIBS dlllist.
  name = arg(1)
  level = arg(2)
  -- say name
  "objdump -x" name '| awk "/DLL/ { print $3 }" | grep -v api-ms-win-crt | rxqueue'
  do while queued() > 0
	parse pull dll
	dll = ARMLIBS || dll
	if dll \= name then do
		if stream(dll, "c", "query exists") \= "" then do
			found = 0
			do i = 1 to dlllist.0
				if dlllist.i = dll then do
					-- say "Found!"
					found = 1
				end
			end
			if found = 0 then do
				-- say copies(" ", level) || dll
				dllcount = dlllist.0 + 1
				dlllist.dllcount = dll
				dlllist.0 = dllcount
			end
		end
	end
  end
  return