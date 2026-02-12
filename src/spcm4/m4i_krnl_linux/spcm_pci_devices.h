// spcm_pci_devices:
// list of all supported PCI devices of the Spectrum M4i series
// (c) Spectrum GmbH

static struct pci_device_id g_stPCIIds[] =
   {
       { PCI_DEVICE(SPC_VENDOR_ID, 0x2100), },   // M4i.44xx-x8
       { PCI_DEVICE(SPC_VENDOR_ID, 0x2101), },   // M4i.22xx-x8
       { PCI_DEVICE(SPC_VENDOR_ID, 0x2102), },   // M4i.66xx-x8
       { PCI_DEVICE(SPC_VENDOR_ID, 0x2103), },   // M4i.77xx-x8
       { PCI_DEVICE(SPC_VENDOR_ID, 0x2110), },   // M2p.59xx-x4
       { PCI_DEVICE(SPC_VENDOR_ID, 0x2111), },   // M2p.65xx-x4
       { PCI_DEVICE(SPC_VENDOR_ID, 0x2112), },   // M2p.75xx-x4
       { PCI_DEVICE(SPC_VENDOR_ID, 0x2120), },   // M5i.33xx-x16
       { PCI_DEVICE(SPC_VENDOR_ID, 0x2121), },   // M5i.63xx-x16
       { 0, }
   };
