#!/bin/sh
# tools/deploy.sh [sshhost] [port]
#
# Put the authority on a machine and keep it there.
#
#   tools/deploy.sh macpro 9017
#
# Ships source, not a binary.  The engine is ISO C99 with no POSIX in it
# except the one file that knows about sockets, so it builds wherever it
# lands -- and building there is what caught a C11 typedef this compiler
# allowed and the server's did not.  A binary would have hidden that.
set -e
HOST=${1:-macpro}
PORT=${2:-9017}
DIR=court-c
cd "$(dirname "$0")/.."

tar czf /tmp/court-src.tgz --exclude='*.o' \
    src Makefile CARDS.tsv gen-cards.py extract-cards.py design.xml
scp -q /tmp/court-src.tgz "$HOST":/tmp/

ssh "$HOST" "set -e
  mkdir -p ~/$DIR && cd ~/$DIR
  rm -rf src court
  tar xzf /tmp/court-src.tgz
  make 2>&1 | grep -E 'error:|warning:' && exit 1
  echo 'built clean'
  ./court --seed 1 --houses 2 >/dev/null && echo 'plays a game'

  cat > ~/Library/LaunchAgents/uk.rhianor.court.plist <<PLIST
<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
  <key>Label</key><string>uk.rhianor.court</string>
  <key>ProgramArguments</key>
  <array>
    <string>\$HOME/$DIR/court</string>
    <string>--serve</string><string>0</string>
    <string>--houses</string><string>2</string>
    <string>--port</string><string>$PORT</string>
    <string>--central</string>
  </array>
  <key>WorkingDirectory</key><string>\$HOME/$DIR</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>\$HOME/$DIR/court.log</string>
  <key>StandardErrorPath</key><string>\$HOME/$DIR/court.log</string>
</dict>
</plist>
PLIST
  launchctl unload ~/Library/LaunchAgents/uk.rhianor.court.plist 2>/dev/null || true
  launchctl load ~/Library/LaunchAgents/uk.rhianor.court.plist
  sleep 2
  netstat -an -p tcp | grep LISTEN | grep -q '$PORT' \
    && echo 'listening on $PORT' || { echo 'NOT listening'; exit 1; }
"
echo "deployed to $HOST:$PORT"
