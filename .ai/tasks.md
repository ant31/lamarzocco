[ ] Long press display timer and it starts quick pooling to display timer and weight (if available) as soon as the machine start brewing.
    It must get the brewing time as accurate as possible. Add manuall start /stop button too
    The timer must show the number in big to the decimal
[ ] Wire brew_counter_update() to a real brewing_active signal once machine link exposes it.
    Infrastructure is in place (brew_counter.h/c, NVS keys, dashboard badge slot).

[x] Dashboard read-only, combined pre-brew page, encoder navigates pages.
    NOTE: Dashboard is now pure display (clock + large temp), no selection/edit.
    CTRL_FOCUS_PREBREW is a new combined page: click→select, click→edit, click→confirm.
    Timeout reverts at any stage. Encoder navigates pages when not in select/edit mode.
    Fixed bug where DASHBOARD focus (enum value > BBW) was incorrectly reset to TEMPERATURE
    on every machine value sync.
[x] In the connect qr code page, add a 'connect' that will try to connect to the existing machine (the same thing that it does at the startup)
    NOTE: "Connect" button added to the QR/setup screen. Dispatches CONNECT_MACHINE event
    which calls lm_ctrl_machine_link_request_sync_mode(ALL). Hidden during reset flows.
[x] Combine the pages pre-brewing, the top area display the in, and bottom area the out. Add a way to navigate between both to edit them . Not sure what's best way why with the encoder it switch with top/bottom, and press activate the edit, then encoder to change value, and press again to confirm.
    NOTE: Addressed by the dashboard — INFUSE and PAUSE share the dashboard view as a
    single page dot. Encoder cycles TEMP→INFUSE→PAUSE, press enters edit, press confirms,
    timeout reverts. Page dot now collapses all three to one dashboard dot.
[x] The home page should be a sort of dashboard. It must shows the temp, pre-brew timing,a clock.  can be configured in webpage setting. Do a 24h digital clock. thing of like a smart watch. it's a big 480/480 screen so there's space. Pre-brew in/out can be small and combined.   so maybe a 3 part dashboard for now
like clock half left, brewing temp top right, pre-brew bottom right.
    NOTE: Dashboard renders on CTRL_SCREEN_MAIN when focus is TEMP/INFUSE/PAUSE.
    Encoder rotates between panels (nav mode), press activates edit, press again confirms.
    Timeout reverts unconfirmed edits. Swipe to STEAM/STANDBY/BBW single-value pages.
[x] Add a counter of number of coffee brewed in the day. A coffee brewed is brewing for more than 10s.
    NOTE: brew_counter module created (brew_counter.h/c). Persists count + date_yday to NVS.
    Dashboard shows ☕ N badge. Wiring to real brewing signal is a follow-up task.
[x] when sliding before the QR code, it show a settings page, where you can change the color them (let user rotate theme) a them is combinaison of background/forground colors
    can change the luminosity of the device. Add a reset default button.
    NOTE: settings/backflush are now proper first-class screens (not overlays) so theme/bg applies correctly.

