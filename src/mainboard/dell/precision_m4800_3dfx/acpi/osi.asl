Scope (_SB)
{
    Name (W98S, "Microsoft Windows")
    Name (NT5S, "Microsoft Windows NT")
    Name (WINM, "Microsoft WindowsME: Millennium Edition")
    Name (WXP, "Windows 2001")
    Name (WLG, "Windows 2006")
    Name (WIN7, "Windows 2009")
    Name (WIN8, "Windows 2012")
    Name (LINX, "Linux")
    Scope (_SB)
    {
        Name (ACOS, Zero)
        Name (ACSE, Zero)
        Method (OSID, 0, NotSerialized)
        {
            If ((ACOS == Zero))
            {
                ACOS = One
                ACSE = Zero
                If (CondRefOf (\_OSI))
                {
                    If (_OSI (WXP))
                    {
                        ACOS = 0x10
                    }

                    If (_OSI (WLG))
                    {
                        ACOS = 0x20
                    }

                    If (_OSI (WIN7))
                    {
                        ACOS = 0x80
                    }

                    If (_OSI (WIN8))
                    {
                        ACOS = 0x80
                        ACSE = One
                    }

                    If (_OSI (LINX))
                    {
                        ACOS = 0x40
                    }
                }
                Else
                {
                    If (STRE (_OS, W98S))
                    {
                        ACOS = 0x02
                    }

                    If (STRE (_OS, WINM))
                    {
                        ACOS = 0x04
                    }

                    If (STRE (_OS, NT5S))
                    {
                        ACOS = 0x08
                    }
                }
            }

            Return (ACOS) /* \_SB_.ACOS */
        }
 }

    Method (BBRD, 2, NotSerialized)
    {
        CreateByteField (Arg0, Arg1, VAL)
        Return (VAL) /* \BBRD.VAL_ */
    }

    Method (STRE, 2, NotSerialized)
    {
        Name (STR1, Buffer (0x50){})
        Name (STR2, Buffer (0x50){})
        STR1 = Arg0
        STR2 = Arg1
        Local0 = Zero
        Local1 = One
        While (Local1)
        {
            Local1 = BBRD (STR1, Local0)
            Local2 = BBRD (STR2, Local0)
            If ((Local1 != Local2))
            {
                Return (Zero)
            }

            Local0++
        }

        Return (One)
    }

}
