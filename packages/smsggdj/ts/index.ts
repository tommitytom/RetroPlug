// The package's surface: the sms-sync role (its work is rp.sms.sync), the ROM provider that attaches it and the tracker
// integration its Songs/Assets menus are built over. The battery codec, the layout reader and the sync encoder are
// subpaths (./codec/sav, ./runtime/layout, ./smsSync), where a caller names the one it reads. Importing this barrel
// registers nothing: the app's own registration is ./app, which the generated registry imports by name.
export { SMS_SYNC_ROLE } from "./app";
export { SMSGGDJ_ROM_PROVIDER } from "./romProvider";
export { smsggdjIntegration } from "./smsggdjIntegration";
