-- Fingerprints Launcher: play a performance, or a Studio grid, without a terminal window.
-- Double-click: choose from the performances and the Studio grids, in a list under two headings; a Studio grid then takes a rig. Drop a performance folder,
-- a performance.json or a studio.json on it: play that. It opens Fingerprints.app by its bundle id
-- with --launch (ofxMarkSynth LaunchComposer). Built by build-launcher.sh.
-- It remembers the last launch in the defaults domain com.meyfroidt.fingerprint2.launcher and offers it first.

property performancesRoot : "Documents/MarkSynth-performances"
property appId : "com.meyfroidt.fingerprint2"
property lastDomain : "com.meyfroidt.fingerprint2.launcher"

on run
	-- The last launch, when its files are still there: play it again, or choose.
	set lastLaunch to readLast()
	if lastLaunch is not missing value then
		set answer to button returned of (display dialog "Play again:" & return & return & (label of lastLaunch) buttons {"Cancel", "Choose...", "Play again"} default button "Play again" cancel button "Cancel" with title "Fingerprints")
		if answer is "Play again" then
			launchIt(launchFile of lastLaunch, gridPath of lastLaunch, rigPath of lastLaunch, mode of lastLaunch, label of lastLaunch)
			return
		end if
	end if
	set root to (POSIX path of (path to home folder)) & performancesRoot
	-- One list in two sections, each under a heading line; picking a heading asks again.
	set choices to {}
	set targets to {}
	-- The performances: each folder of Performances that holds a performance.json.
	try
		set found to paragraphs of (do shell script "cd " & quoted form of (root & "/Performances") & " && for d in */; do [ -f \"$d/performance.json\" ] && echo \"${d%/}\"; done; true")
		repeat with name in found
			if name as text is not "" then
				if choices is {} then
					set end of choices to "PERFORMANCES"
					set end of targets to {"", "", "heading", ""}
				end if
				set end of choices to "    " & name
				set end of targets to {root & "/Performances/" & name & "/performance.json", "", "performance", name as text}
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
		set headed to false
		repeat with gridPath in grids
			if gridPath as text is not "" then
				if not headed then
					set end of choices to "STUDIO GRIDS"
					set end of targets to {"", "", "heading", ""}
					set headed to true
				end if
				set gridName to (do shell script "basename " & quoted form of (gridPath as text))
				set end of choices to "    " & gridName
				set end of targets to {studioFile, gridPath as text, "studio", gridName}
			end if
		end repeat
	end try
	if choices is {} then
		display alert "Nothing to play" message "No performance.json under " & root & "/Performances and no grids in " & studioFile
		return
	end if
	set target to {"", "", "heading", ""}
	repeat while item 3 of target is "heading"
		set picked to choose from list choices with title "Fingerprints" with prompt "Play:" OK button name "Play"
		if picked is false then return
		repeat with i from 1 to count of choices
			if item i of choices is (item 1 of picked) then
				set target to item i of targets
				exit repeat
			end if
		end repeat
	end repeat
	set kind to item 3 of target
	set chosenName to item 4 of target
	set rigPath to ""
	set rigLabel to ""
	set mode to ""
	-- The Studio joins a grid and a rig at launch: the rigs of studio.json's rigFolders, its default first.
	if kind is "studio" then
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
		set pickedRig to choose from list rigNames with title "Fingerprints" with prompt "Play " & chosenName & " through the rig:" default items {item 1 of rigNames} OK button name "Play"
		if pickedRig is false then return
		repeat with i from 1 to count of rigNames
			if item i of rigNames is (item 1 of pickedRig) then
				set rigPath to item i of rigs as text
				set rigLabel to " through the rig " & (item i of rigNames)
			end if
		end repeat
	end if
	if kind is "performance" then
		set answer to button returned of (display dialog "Play " & chosenName & " for the show, or as a practice (no take on the first wake, no auto drawings)?" buttons {"Cancel", "Practice", "Show"} default button "Show" with title "Fingerprints")
		if answer is "Practice" then set mode to "practice"
	end if
	if kind is "studio" then
		set chosenLabel to "the Studio grid " & chosenName & rigLabel
	else
		set chosenLabel to "the performance " & chosenName
	end if
	if mode is "practice" then set chosenLabel to chosenLabel & " (practice)"
	launchIt(item 1 of target, item 2 of target, rigPath, mode, chosenLabel)
end run

on open droppedItems
	set dropped to POSIX path of (item 1 of droppedItems)
	launchIt(dropped, "", "", "", "Dropped: " & dropped)
end open

on launchIt(launchFile, gridPath, rigPath, mode, label)
	set command to "open -n -b " & appId & " --args --launch " & quoted form of launchFile
	if gridPath is not "" then set command to command & " --grid " & quoted form of gridPath
	if rigPath is not "" then set command to command & " --rig " & quoted form of rigPath
	if mode is "practice" then set command to command & " --practice"
	writeLast(launchFile, gridPath, rigPath, mode, label)
	do shell script command
end launchIt

-- The last launch, or missing value when there is none or one of its files has gone.
on readLast()
	try
		set launchFile to do shell script "defaults read " & lastDomain & " launchFile"
		set gridPath to do shell script "defaults read " & lastDomain & " gridPath"
		set rigPath to do shell script "defaults read " & lastDomain & " rigPath"
		set mode to do shell script "defaults read " & lastDomain & " mode"
		set label to do shell script "defaults read " & lastDomain & " label"
	on error
		return missing value
	end try
	repeat with p in {launchFile, gridPath, rigPath}
		if (p as text) is not "" then
			try
				do shell script "test -e " & quoted form of (p as text)
			on error
				return missing value
			end try
		end if
	end repeat
	return {launchFile:launchFile, gridPath:gridPath, rigPath:rigPath, mode:mode, label:label}
end readLast

on writeLast(launchFile, gridPath, rigPath, mode, label)
	set keys to {{"launchFile", launchFile}, {"gridPath", gridPath}, {"rigPath", rigPath}, {"mode", mode}, {"label", label}}
	repeat with k in keys
		do shell script "defaults write " & lastDomain & " " & (item 1 of k) & " -string " & quoted form of (item 2 of k as text)
	end repeat
end writeLast
