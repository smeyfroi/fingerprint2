-- Fingerprints Launcher: play a performance, or a Studio grid, without a terminal window.
-- Double-click: choose from the performances and the Studio grids; a Studio grid then takes a rig. Drop a performance folder,
-- a performance.json or a studio.json on it: play that. It opens Fingerprints.app by its bundle id
-- with --launch (ofxMarkSynth LaunchComposer). Built by build-launcher.sh.

property performancesRoot : "Documents/MarkSynth-performances"
property appId : "com.meyfroidt.fingerprint2"

on run
	set root to (POSIX path of (path to home folder)) & performancesRoot
	set choices to {}
	set targets to {}
	-- The performances: each folder of Performances that holds a performance.json.
	try
		set found to paragraphs of (do shell script "cd " & quoted form of (root & "/Performances") & " && for d in */; do [ -f \"$d/performance.json\" ] && echo \"${d%/}\"; done; true")
		repeat with name in found
			if name as text is not "" then
				set end of choices to "Performance: " & name
				set end of targets to {root & "/Performances/" & name & "/performance.json", ""}
			end if
		end repeat
	end try
	-- The Studio grids: the folders studio.json lists, read by the Studio's own launch model.
	set studioFile to root & "/Studio/studio.json"
	try
		set grids to paragraphs of (do shell script "/usr/bin/python3 -c " & quoted form of ("import json,os,sys
p=sys.argv[1]; d=json.load(open(p)); here=os.path.dirname(p)
for f in d.get('gridFolders',[]):
  r=os.path.normpath(os.path.join(here,os.path.expanduser(f)))
  if os.path.isdir(r):
    for n in sorted(os.listdir(r)):
      if os.path.isdir(os.path.join(r,n,'config','synth')): print(os.path.join(r,n))") & " " & quoted form of studioFile)
		repeat with gridPath in grids
			if gridPath as text is not "" then
				set end of choices to "Studio: " & (do shell script "basename " & quoted form of (gridPath as text))
				set end of targets to {studioFile, gridPath as text}
			end if
		end repeat
	end try
	if choices is {} then
		display alert "Nothing to play" message "No performance.json under " & root & "/Performances and no grids in " & studioFile
		return
	end if
	set picked to choose from list choices with title "Fingerprints" with prompt "Play:" OK button name "Play"
	if picked is false then return
	set chosen to item 1 of picked
	repeat with i from 1 to count of choices
		if item i of choices is chosen then
			set target to item i of targets
			exit repeat
		end if
	end repeat
	set extra to ""
	-- The Studio joins a grid and a rig at launch: the rigs of studio.json's rigFolders, its default first.
	if chosen starts with "Studio: " then
		set rigs to paragraphs of (do shell script "/usr/bin/python3 -c " & quoted form of ("import json,os,sys
p=sys.argv[1]; d=json.load(open(p)); here=os.path.dirname(p)
first=os.path.normpath(os.path.join(here,os.path.expanduser(d.get('rig',''))))
found=[first] if os.path.isfile(first) else []
for f in d.get('rigFolders',[]):
  r=os.path.normpath(os.path.join(here,os.path.expanduser(f)))
  if os.path.isdir(r):
    for n in sorted(os.listdir(r)):
      q=os.path.join(r,n)
      if n.endswith('.json') and q not in found: found.append(q)
print(chr(10).join(found))") & " " & quoted form of studioFile)
		set rigNames to {}
		repeat with rigPath in rigs
			set rigNames to rigNames & {(do shell script "basename " & quoted form of (rigPath as text) & " .json")}
		end repeat
		set pickedRig to choose from list rigNames with title "Fingerprints" with prompt "Play " & (text 9 thru -1 of chosen) & " through the rig:" default items {item 1 of rigNames} OK button name "Play"
		if pickedRig is false then return
		repeat with i from 1 to count of rigNames
			if item i of rigNames is (item 1 of pickedRig) then set extra to " --rig " & quoted form of (item i of rigs as text)
		end repeat
	end if
	if chosen starts with "Performance: " then
		set mode to button returned of (display dialog "Play " & (text 14 thru -1 of chosen) & " for the show, or as a practice (no take on the first wake, no auto drawings)?" buttons {"Cancel", "Practice", "Show"} default button "Show" with title "Fingerprints")
		if mode is "Practice" then set extra to " --practice"
	end if
	launchIt(item 1 of target, item 2 of target, extra)
end run

on open droppedItems
	set dropped to POSIX path of (item 1 of droppedItems)
	launchIt(dropped, "", "")
end open

on launchIt(launchFile, gridPath, extra)
	set command to "open -n -b " & appId & " --args --launch " & quoted form of launchFile
	if gridPath is not "" then set command to command & " --grid " & quoted form of gridPath
	do shell script command & extra
end launchIt
