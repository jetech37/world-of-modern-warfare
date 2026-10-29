AZEROTH WARFARE
===============

Modern Warfare 2's guns, perks and killstreaks in World of Warcraft 1.12. Private build for
friends; please don't pass it on.

Playing
-------
Double-click Play.bat. The first time, it:
  1. asks for your World of Warcraft 1.12.1 and Modern Warfare 2 folders, unless this folder
     already has them under game\WoW and game\MW2;
  2. extracts the map data from WoW, unless it's already in data\ (several minutes);
  3. creates the database and loads the world (a few minutes).
After that, Play.bat starts the server (if it isn't running) and the game in under a minute.

Make your login first: double-click Create Account.bat and answer its questions. GM level 0 is
a normal player; 3 or more can use the game-master commands.

Stop Server.bat saves every player and stops the server. Closing the game leaves the server
running so you can come straight back.

Playing on a friend's server
----------------------------
Open a command prompt in this folder and run:
    powershell -ExecutionPolicy Bypass -File scripts\aw.ps1 join THEIR-ADDRESS
Play.bat then connects to them instead of starting your own server. "join localhost" switches
back. Your account is made on their server, by them.

Hosting for friends
-------------------
    powershell -ExecutionPolicy Bypass -File scripts\aw.ps1 host YOUR-PUBLIC-ADDRESS
then restart the server, and let TCP ports 3724 and 8085 through your router and firewall.

Where things are
----------------
  var\settings.json   your game folders and server address
  var\db              the database (characters, accounts, buildings)
  var\logs            server logs (Server.log for the world server)
  data\               map data extracted from WoW
The world server's window (minimized on the taskbar) takes GM console commands.
