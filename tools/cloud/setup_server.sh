#!/usr/bin/env bash
# Sets up Phoenix Cloud on a Debian or Ubuntu server, once. Run as root
# (tools/deploy_cloud.py setup uploads this and runs it with sudo):
#
#   setup_server.sh CLOUD_HOST PLAY_HOST EMAIL [--no-tls] [--swap]
#
# It installs Node 22 and the libraries Phoenix needs, makes the service's
# user (phoenix-cloud), its folders (/opt/phoenix-cloud, /var/lib/phoenix-cloud)
# and settings (/etc/phoenix-cloud.env, kept if there already), the systemd
# service, and a site for Apache or nginx (whichever runs here) that passes
# both names to the service; then asks Let's Encrypt for their certificate.
# The service starts with the first deploy. Running it again is safe: it
# keeps the settings, the data and the certificate.
#
# Both names must already point at this server (DNS A records), and the
# firewall must let in ports 80 and 443, before the certificate can be had.

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
BASE=/opt/phoenix-cloud
DATA=/var/lib/phoenix-cloud
ENV_FILE=/etc/phoenix-cloud.env
SERVICE=phoenix-cloud
PORT=8792
PLAY_PORT=8793

say() { printf '\n== %s\n' "$*"; }
die() { printf 'setup: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || die "run as root (sudo)"
[ $# -ge 3 ] || die "usage: setup_server.sh CLOUD_HOST PLAY_HOST EMAIL [--no-tls] [--swap]"
CLOUD_HOST=$1
PLAY_HOST=$2
EMAIL=$3
shift 3
TLS=1
SWAP=0
for arg in "$@"; do
  case "$arg" in
    --no-tls) TLS=0 ;;
    --swap) SWAP=1 ;;
    *) die "unknown option $arg" ;;
  esac
done
[ "$CLOUD_HOST" != "$PLAY_HOST" ] || die "the play origin needs a name of its own"

. /etc/os-release
case "${ID:-} ${ID_LIKE:-}" in
  *debian*|*ubuntu*) ;;
  *) die "this script knows Debian and Ubuntu, not ${PRETTY_NAME:-this system}" ;;
esac
[ ! -d /opt/bitnami ] || die "this is a Bitnami image, whose Apache lives in /opt/bitnami; set the site up by hand (see cloud/README.md)"

say "Packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q curl ca-certificates gnupg tar gzip
# The sandbox Phoenix opens uploaded files in (cloud/lib/sandbox.mjs).
apt-get install -y -q bubblewrap
# What Phoenix's AppImage needs from the system to run with no window.
# Names differ between releases (Ubuntu 24.04 adds t64), so each is tried.
for p in libgl1 libegl1 libopengl0 libfontconfig1 libfreetype6 libxkbcommon0 libglib2.0-0 libdbus-1-3 libnss3 libasound2; do
  apt-get install -y -q "$p" >/dev/null 2>&1 || apt-get install -y -q "${p}t64" >/dev/null 2>&1 || echo "  (no $p here; Phoenix may not need it)"
done

say "Node 22"
node_ok() {
  command -v node >/dev/null || return 1
  local v
  v=$(node -p 'process.versions.node')
  local major=${v%%.*} rest=${v#*.}
  local minor=${rest%%.*}
  # node:sqlite without a flag needs 22.13 or later.
  [ "$major" -gt 22 ] || { [ "$major" -eq 22 ] && [ "$minor" -ge 13 ]; }
}
if ! node_ok; then
  curl -fsSL https://deb.nodesource.com/setup_22.x | bash -
  apt-get install -y -q nodejs
fi
node_ok || die "Node 22.13 or later is needed, and $(node -v 2>/dev/null || echo none) is here"
echo "node $(node -v)"

say "User and folders"
id -u phoenix-cloud >/dev/null 2>&1 || useradd --system --home-dir "$DATA" --shell /usr/sbin/nologin phoenix-cloud
install -d -m 755 -o root -g root "$BASE" "$BASE/bin" "$BASE/releases" "$BASE/editors" "$BASE/phoenix-builds"
install -d -m 750 -o phoenix-cloud -g phoenix-cloud "$DATA" "$DATA/home" "$DATA/backups"
install -m 755 "$HERE/phoenix-cloud-admin" "$BASE/bin/phoenix-cloud-admin"
ln -sfn "$BASE/bin/phoenix-cloud-admin" /usr/local/bin/phoenix-cloud-admin
# Until a Phoenix is installed (deploy --phoenix) there is none, and the
# service runs without new documents and conversions.

say "Settings ($ENV_FILE)"
if [ -f "$ENV_FILE" ]; then
  echo "kept as it is"
else
  ADMIN_PASSWORD=$(head -c 18 /dev/urandom | base64 | tr -d '/+=' | head -c 20)
  umask 027
  cat > "$ENV_FILE" <<EOF
# Phoenix Cloud's settings (cloud/README.md lists them all). Read by the
# service at start: sudo phoenix-cloud-admin restart after a change.
PHOENIX_CLOUD_URL=https://$CLOUD_HOST
PHOENIX_CLOUD_PLAY_URL=https://$PLAY_HOST
PHOENIX_CLOUD_PORT=$PORT
PHOENIX_CLOUD_PLAY_PORT=$PLAY_PORT
PHOENIX_CLOUD_DATA=$DATA
PHOENIX_CLOUD_EDITOR=$BASE/editor
PHOENIX_CLOUD_RESOURCES=$BASE/current/resources
PHOENIX_CLOUD_PHOENIX=$BASE/bin/phoenix
# The first admin (@phoenix) gets this password when the database is made.
PHOENIX_CLOUD_ADMIN_PASSWORD=$ADMIN_PASSWORD
HOME=$DATA/home
# Sign in with Google or GitHub:
# PHOENIX_CLOUD_GOOGLE_ID=
# PHOENIX_CLOUD_GOOGLE_SECRET=
# PHOENIX_CLOUD_GITHUB_ID=
# PHOENIX_CLOUD_GITHUB_SECRET=
# Email through Amazon SES (its SMTP settings; the domain verified there):
# PHOENIX_CLOUD_SMTP_HOST=email-smtp.us-west-2.amazonaws.com
# PHOENIX_CLOUD_SMTP_PORT=587
# PHOENIX_CLOUD_SMTP_USER=
# PHOENIX_CLOUD_SMTP_PASSWORD=
# PHOENIX_CLOUD_MAIL_FROM=Phoenix Cloud <no-reply@phoenixanimate.com>
# PHOENIX_CLOUD_MAIL_REPLY_TO=
EOF
  umask 022
  chown root:phoenix-cloud "$ENV_FILE"
  chmod 640 "$ENV_FILE"
  echo "written; the first admin is @phoenix with the password $ADMIN_PASSWORD"
  echo "(it is also in $ENV_FILE; change it after signing in)"
fi

say "Service"
install -m 644 "$HERE/phoenix-cloud.service" "/etc/systemd/system/$SERVICE.service"
# A ceiling for the whole service (Phoenix's conversions have their own,
# smaller one): most of the memory, leaving room for the web server.
mem_kb=$(awk '/^MemTotal:/ { print $2 }' /proc/meminfo)
install -d "/etc/systemd/system/$SERVICE.service.d"
printf '[Service]\nMemoryMax=%dM\n' $((mem_kb * 7 / 10 / 1024)) > "/etc/systemd/system/$SERVICE.service.d/memory.conf"
systemctl daemon-reload
systemctl enable "$SERVICE" >/dev/null
echo "enabled; it starts with the first deploy"

if [ "$SWAP" = 1 ] && ! swapon --show | grep -q .; then
  say "Swap"
  fallocate -l 2G /swapfile && chmod 600 /swapfile && mkswap /swapfile >/dev/null && swapon /swapfile
  grep -q '^/swapfile ' /etc/fstab || echo '/swapfile none swap sw 0 0' >> /etc/fstab
  echo "2 GB of swap at /swapfile"
fi

fill() {
  sed -e "s/@CLOUD_HOST@/$CLOUD_HOST/g" -e "s/@PLAY_HOST@/$PLAY_HOST/g" -e "s/@PORT@/$PORT/g" -e "s/@PLAY_PORT@/$PLAY_PORT/g" "$1"
}

say "Web server"
# Whichever already serves the site here; Apache when neither does.
if systemctl is-active --quiet nginx && ! systemctl is-active --quiet apache2; then
  WEB=nginx
  fill "$HERE/nginx-vhost.conf" > /etc/nginx/sites-available/phoenix-cloud
  ln -sfn /etc/nginx/sites-available/phoenix-cloud /etc/nginx/sites-enabled/phoenix-cloud
  nginx -t
  systemctl reload nginx
else
  WEB=apache
  apt-get install -y -q apache2 >/dev/null
  a2enmod -q proxy proxy_http headers rewrite ssl
  fill "$HERE/apache-vhost.conf" > /etc/apache2/sites-available/phoenix-cloud.conf
  a2ensite -q phoenix-cloud
  apache2ctl configtest
  systemctl reload apache2
fi
echo "$WEB serves $CLOUD_HOST and $PLAY_HOST"

if [ "$TLS" = 1 ]; then
  say "HTTPS (Let's Encrypt)"
  apt-get install -y -q certbot "python3-certbot-$WEB" >/dev/null
  if certbot certificates 2>/dev/null | grep -q "Domains: .*$CLOUD_HOST"; then
    echo "a certificate for $CLOUD_HOST is already here"
  else
    certbot "--$WEB" -d "$CLOUD_HOST" -d "$PLAY_HOST" --non-interactive --agree-tos -m "$EMAIL" --redirect \
      || die "certbot failed: check that $CLOUD_HOST and $PLAY_HOST point at this server and ports 80 and 443 are open, then run setup again"
  fi
fi

say "Done"
echo "Next, from your PC: python tools/deploy_cloud.py deploy --editor --phoenix release"
