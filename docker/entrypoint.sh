#!/usr/bin/env bash

# Runtime-configurable user
USER_NAME="${USER_NAME:-demo}"
USER_UID="${USER_UID:-1000}"
USER_GID="${USER_GID:-1000}"

echo "[entrypoint] USER_NAME=${USER_NAME}, UID=${USER_UID}, GID=${USER_GID}"

# Ensure a group exists for this GID (prefer same name as the user)
if ! getent group "${USER_GID}" >/dev/null 2>&1; then
    # Create group with given GID and name = USER_NAME
    groupadd -g "${USER_GID}" "${USER_NAME}"
fi

# Create user if it doesn't exist
if ! id -u "${USER_NAME}" >/dev/null 2>&1; then
    useradd -m -u "${USER_UID}" -g "${USER_GID}" -s /bin/bash "${USER_NAME}"
fi

# Add user to nrf group so they can access /opt/nrf
usermod -a -G nrf,sudo "${USER_NAME}"
# Allow passwordless sudo for this user
echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/${USER_NAME}"
chmod 0440 "/etc/sudoers.d/${USER_NAME}"

export HOME="/home/${USER_NAME}"
mkdir -p "${HOME}"

# TOOLCHAIN_DB_DIR="${ZEPHYR_BASE}/.cache/ToolchainCapabilityDatabase"

# mkdir -p "${TOOLCHAIN_DB_DIR}"
# root:nrf owns cache, group has rwx and setgid so new files stay in group
# chown -R root:nrf "${ZEPHYR_BASE}/.cache"
# chmod -R g+rwxs "${ZEPHYR_BASE}/.cache"

# chmod -R g+rwx "${NRFUTIL_HOME}/logs"

user_bashrc="/home/${USER_NAME}/.bashrc"

{
    echo "export NRF=\"${NRF}\""
    echo "export NRFUTIL=\"${NRFUTIL}\""
    echo "export NRFUTIL_HOME=\"${NRFUTIL_HOME}\""
    echo "export NCS_VERSION=\"${NCS_VERSION}\""
    echo "export NCS=\"${NCS}\""
    echo "export NCS_SDK=\"${NCS_SDK}\""
    echo "export ZEPHYR_BASE=\"${ZEPHYR_BASE}\""
    echo "export ZEPHYR_TOOLCHAIN_VARIANT=\"${ZEPHYR_TOOLCHAIN_VARIANT}\""
    echo "export ZEPHYR_SDK_INSTALL_DIR=\"${ZEPHYR_SDK_INSTALL_DIR}\""
    echo "export BOARD=\"${BOARD}\""
    echo "export PATH=\"/home/${USER_NAME}/.local/bin:\$PATH\""
} >> "$user_bashrc"

echo "[entrypoint] Running as ${USER_NAME} (uid=${USER_UID}, gid=${USER_GID}), groups: $(id -nG "${USER_NAME}")"

# Start a proper login shell as that user
# exec su - "${USER_NAME}"

# -----------------------------
# Command dispatch
# -----------------------------

# If a command was passed to `docker run ... IMAGE <cmd> ...`
if [ "$#" -gt 0 ]; then
    # One-shot mode: run the given command as the user, then exit
    exec sudo -E -u "${USER_NAME}" "$@"
else
    # Interactive mode: start a login shell as the user, in WORKDIR
    exec su - "${USER_NAME}"
fi