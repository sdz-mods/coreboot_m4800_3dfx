Scope (\_SB)
{

Mutex (ECM1, 0x01)

        Method (ECWB, 2, NotSerialized)
        {
            \_SB.PCI0.LPCB.ECDV.ECW1 (Arg0, Arg1)
        }

        Method (ECRW, 1, NotSerialized)
        {
            Return (\_SB.PCI0.LPCB.ECDV.ECR2 (Arg0))
        }

        Method (ECRB, 1, NotSerialized)
        {
            Return (\_SB.PCI0.LPCB.ECDV.ECR1 (Arg0))
        }

        Method (ECM8, 1, NotSerialized)
        {
            ECWB (0x04, Arg0)
            Name (LBUF, Buffer (0x21){})
            Local0 = Zero
            While ((Local0 < 0x20))
            {
                Local1 = ECRB (0x2A)
                LBUF [Local0] = Local1
                If ((Local1 == Zero))
                {
                    Break
                }

                Local0++
            }

            If ((Local1 != Zero))
            {
                LBUF [Local0] = Zero
                Local0++
            }

            Local0++
            Name (OBUF, Buffer (Local0){})
            OBUF = LBUF /* \ECM8.LBUF */
            Return (OBUF) /* \ECM8.OBUF */
        }

        Method (ECG5, 0, NotSerialized)
        {
            Local0 = ECRB (0x06)
            Return (Local0)
        }

        Method (ECG6, 2, NotSerialized)
        {
            Acquire (ECM1, 0xFFFF)
            Local2 = ECG2 ()
            ECWB (0x03, Arg0)
            Arg1 [Zero] = ECRB (0x10)  //Battery state
            Local0 = ECRW (0x12)
            If ((Local0 == Zero))
            {
                Local0++
            }
            ElseIf ((Local2 != Zero))
            {
                If ((Local0 & 0x8000))
                {
                    Local0 = Ones
                }
            }
            ElseIf ((Local0 & 0x8000))
            {
                Local0 = (Zero - Local0)
                Local0 &= 0xFFFF
            }
            Else
            {
                Local0 = Ones
            }

            Arg1 [One] = Local0  //Battery present rate
            Local0 = ECRW (0x16)
            Arg1 [0x02] = Local0  //Battery remaining capacity
            Local0 = ECRW (0x14)
            Arg1 [0x03] = Local0  //Battery present voltage
            Release (ECM1)
        }

        Method (ECG2, 0, NotSerialized)
        {
            Return (ECBT (Zero, One))
        }

        Method (ECBT, 2, NotSerialized)
        {
            Local0 = \_SB.PCI0.LPCB.ECDV.ECR1 (Arg0)
            Local0 &= Arg1
            If (Local0)
            {
                Return (One)
            }

            Return (Zero)
        }

    Method (XPTB, 1, NotSerialized)
    {
        Local0 = SizeOf (Arg0)
        If ((ObjectType (Arg0) == 0x02))
        {
            Local0++
        }

        Name (OBUF, Buffer (Local0){})
        OBUF = Arg0
        If ((ObjectType (Arg0) == 0x02))
        {
            Local0--
            OBUF [Local0] = Zero
        }

        Return (OBUF) /* \XPTB.OBUF */
    }


    Method (STDG, 3, NotSerialized)
    {
        Local0 = Arg0
        If ((Arg0 >= 0x0A))
        {
            Divide (Arg0, 0x0A, Local0, Local1)
            Arg2 = STDG (Local1, Arg1, Arg2)
        }

        Local0 += 0x30
        Arg1 [Arg2] = Local0
        Arg2++
        Return (Arg2)
    }

    Method (XPTS, 1, NotSerialized)
    {
        Name (LBUF, Buffer (0x20){})
        Local0 = STDG (Arg0, LBUF, Zero)
        LBUF [Local0] = Zero
        Local0++
        Name (OBUF, Buffer (Local0){})
        OBUF = LBUF /* \XPTS.LBUF */
        Return (OBUF) /* \XPTS.OBUF */
    }

        Method (ECU0, 2, NotSerialized)
        {
            Local0 = One
            Local1 = Zero
            While ((Local1 != 0xFF))
            {
                Local1 = DerefOf (Arg0 [Local0])
                If ((Arg1 == Local1))
                {
                    Local0++
                    Local2 = DerefOf (Arg0 [Local0])
                    Local2 = XPTB (Local2)
                    Return (Local2)
                }

                Local0 += 0x02
            }

            Local2 = DerefOf (Arg0 [Zero])
            Local2 = ECM8 (Local2)
            Return (Local2)
        }

        Method (ECG9, 2, NotSerialized)
        {
            Acquire (ECM1, 0xFFFF)
            ECWB (0x03, Arg0)
            Arg1 [Zero] = One         //1  Power Unit report mAh
            Local0 = ECRW (0x20)
            Arg1 [One] = Local0       //2  Design Capacity
            Local1 = ECRW (0x1E)
            Arg1 [0x02] = Local1      //3  Last Full Charge Capacity
            Arg1 [0x03] = One         //4  Battery Technology
            Local2 = ECRW (0x22)
            Arg1 [0x04] = Local2      //5  Design Voltage
            Divide (Local0, 0x0A, Local5, Local3)
            Arg1 [0x05] = Local3      //6  Design Capacity of Warning
            Divide (Local0, 0x21, Local5, Local3)
            Arg1 [0x06] = Local3      //7  Design Capacity of Low
            Divide (Local0, 0x64, Local5, Local3)
            Arg1 [0x07] = Local3      //8  Battery Capacity Granularity 1
            Arg1 [0x08] = Local3      //9  Battery Capacity Granularity 2
            Local3 = ECU0 (BS01, Zero)
            Arg1 [0x09] = Local3      //10  Model Number
            Local3 = ECRW (0x26)
            Local3 = XPTS (Local3)
            Arg1 [0x0A] = Local3      //11  Serial Number
            Local3 = ECRB (0x29)
            Local3 = ECU0 (BS03, Local3)
            Arg1 [0x0B] = Local3      //12  Battery Type
            Local3 = ECRB (0x28)
            Local3 = ECU0 (BS02, Local3)
            Arg1 [0x0C] = Local3      //13  OEM Information
            Local5 = Local5 //dummy operation
            Release (ECM1)
        }

        Name (BS01, Package (0x03)
        {
            One,
            0xFF,
            "Unknown"
        })

        Name (BS02, Package (0x0F)
        {
            0x03,
            0x02,
            "Sony",
            0x03,
            "Sanyo",
            0x04,
            "Panasonic",
            0x07,
            "SMP",
            0x08,
            "Motorola",
            0x06,
            "Samsung SDI",
            0xFF,
            "Unknown"
        })

        Name (BS03, Package (0x13)
        {
            0x02,
            One,
            "PbAc",
            0x02,
            "LION",
            0x03,
            "NiCd",
            0x04,
            "NiMH",
            0x05,
            "NiZn",
            0x06,
            "RAM",
            0x07,
            "ZnAR",
            0x08,
            "LiP",
            0xFF,
            "Unknown"
        })

        Mutex (ECAX, 0x01)
        Method (EEAC, 2, Serialized)
        {
            Acquire (ECAX, 0xFFFF)
            Name (EABF, Buffer (0x08){})
            CreateDWordField (EABF, Zero, ECST)
            CreateDWordField (EABF, 0x04, ECPA)
            ECST = Arg0
            ECPA = Arg1
            EABF = GENS (0x07, EABF, SizeOf (EABF))
            Local0 = ECST /* \_SB_.EEAC.ECST */
            Release (ECAX)
            Return (Local0)
        }


        Device (AC)
        {
            Name (_HID, "ACPI0003" /* Power Source Device */)  // _HID: Hardware ID
            Method (_PCL, 0, NotSerialized)  // _PCL: Power Consumer List
            {
                Return (Package (0x04)
                {
                    _SB,
                    BAT0,
                    BAT1,
                    BAT2
                })
            }

            Method (_PSR, 0, NotSerialized)  // _PSR: Power Source
            {
                Local0 = ECG5 ()
                Local0 &= One
                If ((Local0 != PWRS))
                {
                    PWRS = Local0
                    PNOT ()
                }

                Return (Local0)
            }

            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }
        }

        Method (ACEV, 2, NotSerialized)
        {
            Notify (AC, 0x80) // Status Change
        }


        Device (BAT0)
        {
            Name (_HID, EisaId ("PNP0C0A") /* Control Method Battery */)  // _HID: Hardware ID
            Name (_UID, One)  // _UID: Unique ID
            Name (_PCL, Package (0x01)  // _PCL: Power Consumer List
            {
                _SB
            })
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Local0 = ECG5 ()
                Local0 &= 0x02
                If (Local0)
                {
                    Return (0x1F)
                }

                Return (0x0F)
            }

//            Method (_STA, 0, NotSerialized)  // _STA: Status
//            {
//                Return (0x0F)
//            }


            Method (_BIF, 0, NotSerialized)  // _BIF: Battery Information
            {
                Name (BIF0, Package (0x0D){})
                ECG9 (One, BIF0)
                Return (BIF0) /* \_SB_.BAT0._BIF.BIF0 */
            }

            Method (_BST, 0, NotSerialized)  // _BST: Battery Status
            {
                Name (BST0, Package (0x04){})
                ECG6 (One, BST0)
                Return (BST0) /* \_SB_.BAT0._BST.BST0 */
            }
        }

        Device (BAT1)
        {
            Name (_HID, EisaId ("PNP0C0A") /* Control Method Battery */)  // _HID: Hardware ID
            Name (_UID, 0x02)  // _UID: Unique ID
            Name (_PCL, Package (0x01)  // _PCL: Power Consumer List
            {
                _SB
            })
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                /* Battery presence comes from the EC directly; the OEM
                   firmware also queried its SMM mailbox for the number of
                   supported batteries, which this firmware does not
                   implement. */
                Local0 = ECG5 ()
                Local0 &= 0x08
                If (Local0)
                {
                    Return (0x1F)
                }

                Return (0x0F)
            }

            Method (_BIF, 0, NotSerialized)  // _BIF: Battery Information
            {
                Name (BIF1, Package (0x0D){})
                ECG9 (0x02, BIF1)
                Return (BIF1) /* \_SB_.BAT1._BIF.BIF1 */
            }

            Method (_BST, 0, NotSerialized)  // _BST: Battery Status
            {
                Name (BST1, Package (0x04){})
                ECG6 (0x02, BST1)
                Return (BST1) /* \_SB_.BAT1._BST.BST1 */
            }
        }

        Device (BAT2)
        {
            Name (_HID, EisaId ("PNP0C0A") /* Control Method Battery */)  // _HID: Hardware ID
            Name (_UID, 0x03)  // _UID: Unique ID
            Name (_PCL, Package (0x01)  // _PCL: Power Consumer List
            {
                _SB
            })
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Local0 = ECG5 ()
                Local0 &= 0x20
                If (Local0)
                {
                    Return (0x1F)
                }

                Return (Zero)
            }

            Method (_BIF, 0, NotSerialized)  // _BIF: Battery Information
            {
                Name (BIF1, Package (0x0D){})
                ECG9 (0x03, BIF1)
                Return (BIF1) /* \_SB_.BAT2._BIF.BIF1 */
            }

            Method (_BST, 0, NotSerialized)  // _BST: Battery Status
            {
                Name (BST1, Package (0x04){})
                ECG6 (0x03, BST1)
                Return (BST1) /* \_SB_.BAT2._BST.BST1 */
            }
        }

        Method (BTEV, 2, NotSerialized)
        {
            If ((Arg0 == One))
            {
                If ((Arg1 == Zero))
                {
                    Notify (BAT0, 0x81) // Information Change
                }

                If ((Arg1 == One))
                {
                    Notify (BAT1, 0x81) // Information Change
                }
                Else
                {
                    Notify (BAT2, 0x81) // Information Change
                }
            }

            If ((Arg0 == 0x02))
            {
                If ((Arg1 == Zero))
                {
                    Notify (BAT0, 0x80) // Status Change
                    Notify (BAT0, 0x81) // Information Change
                }

                If ((Arg1 == One))
                {
                    Notify (BAT1, 0x80) // Status Change
                    Notify (BAT1, 0x81) // Information Change
                }
                Else
                {
                    Notify (BAT2, 0x80) // Status Change
                    Notify (BAT2, 0x81) // Information Change
                }
            }

            If ((Arg0 == 0x03))
            {
                If ((Arg1 == Zero))
                {
                    Notify (BAT0, 0x80) // Status Change
                }

                If ((Arg1 == One))
                {
                    Notify (BAT1, 0x80) // Status Change
                }
                Else
                {
                    Notify (BAT2, 0x80) // Status Change
                }
            }
        }

        Name (APRE, Zero)
        Scope (\_SB)
        {
            Method (CBAT, 2, NotSerialized)
            {
                Notify (BAT0, 0x81) // Information Change
                Notify (BAT1, 0x81) // Information Change
                Notify (BAT2, 0x81) // Information Change
                Local0 = ECG5 ()
                APRE = (Local0 & 0x2B)
            }
        }


        Method (SMBI, 2, NotSerialized)
        {
            SNVC (Arg0)
            Local0 = (SMIB + 0x04)
            OperationRegion (WWPR, SystemMemory, Local0, 0x04)
            Field (WWPR, ByteAcc, Lock, Preserve)
            {
                SDW0,   32
            }

            SDW0 = Arg1
            ASMI ()
            Return (SDW0) /* \SMBI.SDW0 */
        }


        Mutex (SMIX, 0x01)
        Name (SMIB, 0xCA7D5000)
        Name (PSMI, 0x000000B2)
        Method (SNVC, 1, NotSerialized)
        {
            OperationRegion (WWPR, SystemMemory, SMIB, 0x04)
            Field (WWPR, DWordAcc, Lock, Preserve)
            {
                SCDW,   32
            }

            SCDW = Arg0
        }

        Method (SMBF, 3, NotSerialized)
        {
            If ((Arg2 > 0x1000))
            {
                Return (Arg1)
            }

            If ((SizeOf (Arg1) < Arg2))
            {
                Return (Arg1)
            }

            SNVC (Arg0)
            Divide (Arg2, 0x04, Local3, Local4)
            Local0 = Local4 //dummy operation
            Local0 = Zero
            While ((Local0 < Local3))
            {
                SNWB (Arg1, Local0)
                Local0++
            }

            While ((Local0 < Arg2))
            {
                SNVP (Arg1, Local0)
                Local0 += 0x04
            }

            ASMI ()
            Local0 = Zero
            While ((Local0 < Local3))
            {
                Arg1 = SNRB (Arg1, Local0)
                Local0++
            }

            While ((Local0 < Arg2))
            {
                Arg1 = SNVG (Arg1, Local0)
                Local0 += 0x04
            }

            Return (Arg1)
        }


        Method (ASMI, 0, NotSerialized)
        {
            OperationRegion (SMIR, SystemIO, PSMI, One)
            Field (SMIR, ByteAcc, Lock, Preserve)
            {
                SCMD,   8
            }

            SCMD = 0x04
        }


        Method (SNWB, 2, NotSerialized)
        {
            Local0 = SMIB /* \SMIB */
            Local0 += Arg1
            Local0 += 0x04
            OperationRegion (WWPR, SystemMemory, Local0, One)
            Field (WWPR, ByteAcc, Lock, Preserve)
            {
                SBY0,   8
            }

            CreateByteField (Arg0, Arg1, SVAL)
            SBY0 = SVAL /* \SNWB.SVAL */
        }

        Method (SNVG, 2, NotSerialized)
        {
            Local0 = SMIB /* \SMIB */
            Local0 += Arg1
            Local0 += 0x04
            OperationRegion (WWPR, SystemMemory, Local0, 0x04)
            Field (WWPR, ByteAcc, Lock, Preserve)
            {
                SDW0,   32
            }

            CreateDWordField (Arg0, Arg1, SVAL)
            SVAL = SDW0 /* \SNVG.SDW0 */
            Return (Arg0)
        }

        Method (SNVP, 2, NotSerialized)
        {
            Local0 = SMIB /* \SMIB */
            Local0 += Arg1
            Local0 += 0x04
            OperationRegion (WWPR, SystemMemory, Local0, 0x04)
            Field (WWPR, ByteAcc, Lock, Preserve)
            {
                SDW0,   32
            }

            CreateDWordField (Arg0, Arg1, SVAL)
            SDW0 = SVAL /* \SNVP.SVAL */
        }

        Method (SNRB, 2, NotSerialized)
        {
            Local0 = SMIB /* \SMIB */
            Local0 += Arg1
            Local0 += 0x04
            OperationRegion (WWPR, SystemMemory, Local0, 0x04)
            Field (WWPR, ByteAcc, Lock, Preserve)
            {
                SBY0,   8
            }

            CreateByteField (Arg0, Arg1, SVAL)
            SVAL = SBY0 /* \SNRB.SBY0 */
            Return (Arg0)
        }

        Method (GENS, 3, NotSerialized)
        {
            Acquire (SMIX, 0xFFFF)
            Local0 = Arg1
            If ((ObjectType (Arg1) == One))
            {
                Local0 = SMBI (Arg0, Arg1)
            }

            If ((ObjectType (Arg1) == 0x03))
            {
                Local0 = SMBF (Arg0, Arg1, Arg2)
            }

            Release (SMIX)
            Return (Local0)
        }


}

