#!/bin/bash
# Run Gradle IN-PROCESS in this sandbox.
#
# Why: a forked Gradle daemon's client<->daemon socket protocol corrupts in
# this sandbox (client dies with "Could not receive a message from the
# daemon" / invalid serialization tag), on both Gradle 8.7 and 8.10.2.
# Running GradleMain in a launcher JVM that (a) carries the Gradle
# instrumentation javaagent and (b) has exactly the JVM args the build's
# gradle.properties demands (org.gradle.jvmargs=-Xmx2g -Dfile.encoding=UTF-8
# plus Gradle's add-opens set and the daemon locale props) makes Gradle
# execute the build in-process — no daemon, no socket.
#
# Also: a merged truststore is REQUIRED for dl.google.com: the sandbox's
# TLS-MITM egress proxy passes Google hosts through with their real cert
# chains, and the JDK's bundled cacerts (118 roots) lacks the root Google
# chains to (the system bundle has 121 and validates fine). We point the JVM
# at a PKCS12 store built from /etc/ssl/certs/ca-certificates.crt, kept at
# ~/workspace/gba-ds-tools/truststore.p12 (password: the standard "changeit").
#
# Also: -Djava.net.preferIPv6Addresses=true is REQUIRED for networking.
# /etc/hosts maps the sandbox egress proxy to a fake IPv4 (198.19.0.1) first
# and its real IPv6 second; Java picks the IPv4 by default and lands in the
# sandbox's TCP policy interceptor ("Other TCP connections is turned off"
# / connection resets). Preferring IPv6 reaches the real proxy.
#
# Usage: gradle-inproc.sh <gradle args...>
set -euo pipefail
export JAVA_HOME="${JAVA_HOME:-$HOME/workspace/snes9x-ra/tools/jdk}"
export GRADLE_USER_HOME="${GRADLE_USER_HOME:-/home/hatch/.gradle}"
GRADLE_HOME="${GRADLE_HOME:-$HOME/workspace/snes9x-ra/tools/gradle}"
LAUNCHER_JAR=$(ls "$GRADLE_HOME"/lib/gradle-launcher-*.jar | head -1)
AGENT_JAR=$(ls "$GRADLE_HOME"/lib/agents/gradle-instrumentation-agent-*.jar | head -1)
exec "$JAVA_HOME/bin/java" \
	--add-opens=java.base/java.util=ALL-UNNAMED \
	--add-opens=java.base/java.lang=ALL-UNNAMED \
	--add-opens=java.base/java.lang.invoke=ALL-UNNAMED \
	--add-opens=java.prefs/java.util.prefs=ALL-UNNAMED \
	--add-opens=java.base/java.nio.charset=ALL-UNNAMED \
	--add-opens=java.base/java.net=ALL-UNNAMED \
	--add-opens=java.base/java.util.concurrent.atomic=ALL-UNNAMED \
	-Xmx2g -Dfile.encoding=UTF-8 -Duser.country=US -Duser.language=en -Duser.variant \
	-Djava.net.preferIPv6Addresses=true \
	-Djavax.net.ssl.trustStore="$HOME/workspace/gba-ds-tools/truststore.p12" \
	-Djavax.net.ssl.trustStorePassword=changeit \
	-Djavax.net.ssl.trustStoreType=PKCS12 \
	-javaagent:"$AGENT_JAR" \
	-cp "$LAUNCHER_JAR" \
	org.gradle.launcher.GradleMain "$@"
