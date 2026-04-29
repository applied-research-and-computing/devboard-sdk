# WiFi Provisioning Debug Log

Branch: `worktree-feat+wifi-provisioning` (tracks main via `worktree-feat+wifi-provisioning`)

## Current Error

```
E (893) protocomm: Security params cannot be null
E (904) network_prov_mgr: Failed to set security endpoint
ESP_ERROR_CHECK failed: esp_err_t 0x102 (ESP_ERR_INVALID_ARG)
expression: network_prov_mgr_start_provisioning(security, pop, service_name, NULL)
  at carbon_wifi.c:192
```

Device boots into provisioning mode correctly (AP `CARBON-E6531C` appears, DHCP
starts at 192.168.4.1) but crashes before the provisioning server is ready.

## What We Know

### sdkconfig is correct

```
CONFIG_CARBON_ENABLE_WIFI_PROVISIONING=y
CONFIG_CARBON_PROVISION_SECURITY_0=y
CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_0=y
```

### The C code path at runtime

`CONFIG_CARBON_PROVISION_SECURITY_0` is defined, so:
- `security = 0` (NETWORK_PROV_SECURITY_0 via integer literal)
- `CONFIG_CARBON_PROVISION_POP` is not defined (depends on SECURITY_1), so `pop = NULL`
- Call: `network_prov_mgr_start_provisioning(0, NULL, "CARBON-E6531C", NULL)`

The `network_prov_mgr` docs say NULL params are valid for Security 0. Yet
`protocomm` rejects it with "Security params cannot be null".

## Hypotheses to Investigate

### 1. Integer literal `0` vs enum `NETWORK_PROV_SECURITY_0`
The security variable is assigned `0` as an integer literal, not the actual
`NETWORK_PROV_SECURITY_0` enum constant. If the enum is not `{ 0, 1, 2 }` — e.g.
if it is `{ 1, 2, 3 }` or has a sentinel at 0 — the wrong security handler would
be selected internally, causing the null-params error.

**Try:** Replace integer literals with the actual enum constants:
```c
network_prov_security_t security =
#ifdef CONFIG_CARBON_PROVISION_SECURITY_0
    NETWORK_PROV_SECURITY_0;
#elif defined(CONFIG_CARBON_PROVISION_SECURITY_1)
    NETWORK_PROV_SECURITY_1;
#else
    NETWORK_PROV_SECURITY_2;
#endif
```

### 2. Security 0 actually requires a non-null params struct in network_provisioning v1.2.4
The manager.h docs say NULL is fine, but the protocomm layer beneath it may
have tightened validation in IDF v6.0. Security 0 in older IDF used
`protocomm_security0_params_t` which was typedef'd to `void *` (NULL). It may
now require a pointer to an actual (empty) struct.

**Try:** Pass a dummy struct instead of NULL for Security 0:
```c
#ifdef CONFIG_CARBON_PROVISION_SECURITY_0
    protocomm_security0_params_t sec0_params = {};
    const void *prov_params = &sec0_params;
#else
    const void *prov_params = pop;
#endif
network_prov_mgr_start_provisioning(security, prov_params, service_name, NULL)
```
(Check whether `protocomm_security0_params_t` is defined in
`protocomm/security/protocomm_security.h` or similar.)

### 3. Security 2 is silently selected despite sdkconfig
If the preprocessor is somehow not seeing `CONFIG_CARBON_PROVISION_SECURITY_0`
(stale build cache, include order issue), the `#else` branch fires and sets
`security = 2`. Security 2 requires a non-null `network_prov_security2_params_t *`.

**Try:** Add a compile-time assertion or log the security integer before calling
`network_prov_mgr_start_provisioning`:
```c
ESP_LOGI(TAG, "Starting provisioning with security level: %d", (int)security);
```
Confirm the logged value is `0` before the crash.

### 4. network_prov_mgr_start_provisioning() parameter order differs from wifi_prov
Double-check the full signature in the installed component headers:
```
cat managed_components/espressif__network_provisioning/include/network_provisioning/manager.h \
  | grep -A5 "network_prov_mgr_start_provisioning"
```

## What's Been Done So Far

| Commit | Change |
|--------|--------|
| 8a92d9f | Initial implementation: `carbon_wifi.c`, Kconfig, CMakeLists |
| 5c97213 | Code review fixes: volatile, deadlock, gpio_reset_pin, PoP/security Kconfig |
| e654eae | Migrate all `wifi_prov_*` → `network_prov_*` for IDF v6.0 |
| 9a415cb | Add `main/idf_component.yml` — `wifi_provisioning` is a managed component in IDF 6.0 |
| ac8ad51 | Fix malformed Kconfig security choice block, restore error check |
| ffbe54e | Fix `network_prov_mgr_wait()` void return type |

## Recommended Next Step

Start with hypothesis 1 (enum constants) — it's a one-line change and easy to
confirm. If the security value logs as `0` and the crash still happens, move to
hypothesis 2 (non-null params for security 0).

Also worth reading the installed header directly:
```bash
cat managed_components/espressif__network_provisioning/include/network_provisioning/manager.h
```
This gives the ground truth for what the installed version actually expects,
independent of GitHub docs which may be for a different version.
