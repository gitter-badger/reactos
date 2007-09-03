<if property="ARCH" value="i386">
        <module name="freeldr" type="bootloader">
            <bootstrap base="loader" />
            <library>freeldr_startup</library>
            <library>freeldr_base64k</library>
            <library>freeldr_base</library>
            <library>freeldr_arch</library>
            <library>freeldr_main</library>
            <library>rossym</library>
            <library>cmlib</library>
            <library>rtl</library>
            <library>libcntpr</library>
        </module>
</if>
<if property="ARCH" value="powerpc">
        <module name="ofwldr" type="elfexecutable" buildtype="OFWLDR">
            <library>freeldr_startup</library>
            <library>freeldr_base64k</library>
            <library>freeldr_base</library>
            <library>freeldr_arch</library>
            <library>freeldr_main</library>
            <library>rossym</library>
            <library>cmlib</library>
            <library>rtl</library>
        </module>
</if>
