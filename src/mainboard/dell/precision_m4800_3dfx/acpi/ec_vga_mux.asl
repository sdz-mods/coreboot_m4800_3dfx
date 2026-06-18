Scope (_SB)
{
        Method (VDP1, 2, NotSerialized)
        {
            Local0 = Arg1
            Local0 <<= 0x08
            Local0 |= Arg0
            Local0 = GENS (0x05, Local0, Zero)
            Return (Local0)
        }

}