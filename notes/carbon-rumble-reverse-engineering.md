# Carbon rumble reverse-engineering notes

From the decompiled NFSC.exe supplied during testing:

- Original GameDevice constructor is at 0x00680600.
- Base InputDevice/GameDevice portion ends at offset 0x2C.
- IFeedback subobject begins at +0x2C.
- IFeedback owner/list pointer is at +0x30 and points to GameDevice + 0x04.
- Next GameDevice storage begins at +0x34, so the IFeedback subobject is exactly 8 bytes on x86 (vptr + owner pointer).
- UTL interface-list add function: 0x0060DCB0.
- Carbon IFeedback IHandle: 0x00679170.
- Original GameDevice vtable has an empty method at index 8 (0x00679F90) before GetInterfaces.
- GetInterfaces is vtable index 9 (+0x24) and returns this + 0x2C (0x00680780).
- GetSecondaryDevice is index 10 (+0x28).

Original PC IFeedback vtable at 0x009E2188 has 18 entries. Stack cleanup sizes establish these signatures:

0 dtor
1 no args
2 no args
3 no args
4 no args
5 no args
6 12 bytes (3 args)
7 12 bytes (3 args)
8 12 bytes (3 args)
9 12 bytes (3 args)
10 4 bytes (1 arg)
11 8 bytes (2 args)
12 4 bytes (1 arg)
13 4 bytes (1 arg)
14 4 bytes (1 arg)
15 4 bytes (1 arg)
16 4 bytes (1 arg)
17 8 bytes (2 args)

This differs from the historical generic rumble-test stub, which had too many no-argument game-specific methods for Carbon. Keep Carbon's exact 18-entry ABI.