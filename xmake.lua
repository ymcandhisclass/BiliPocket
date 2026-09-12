add_rules('mode.release', 'mode.debug', 'mode.releasedbg')

set_languages('cxx17', 'c11')
set_warnings('all')
set_exceptions('cxx')

if is_mode('releasedbg') then
    set_symbols('debug')
    set_optimize('fast')
end

target('bili_plugin')
    set_kind('shared')

    add_files('src/*.cpp')
    add_files('src/modules/**/*.cpp')
    add_files('src/*.h')
    add_files('src/modules/**/*.h')
    add_includedirs('src')

    -- Qt5 头文件路径（架构无关）
    if is_plat('linux') then
        if is_arch('x86_64') then
            add_includedirs('/usr/include/x86_64-linux-gnu/qt5')
            add_includedirs('/usr/include/x86_64-linux-gnu/qt5/QtCore')
            add_includedirs('/usr/include/x86_64-linux-gnu/qt5/QtQuick')
            add_includedirs('/usr/include/x86_64-linux-gnu/qt5/QtQml')
            add_includedirs('/usr/include/x86_64-linux-gnu/qt5/QtNetwork')
            add_includedirs('/usr/include/x86_64-linux-gnu/qt5/QtMultimedia')
            add_includedirs('/usr/include/x86_64-linux-gnu/qt5/QtGui')
        elseif is_arch('arm64', 'aarch64') then
            add_includedirs('/usr/include/aarch64-linux-gnu/qt5')
            add_includedirs('/usr/include/aarch64-linux-gnu/qt5/QtCore')
            add_includedirs('/usr/include/aarch64-linux-gnu/qt5/QtQuick')
            add_includedirs('/usr/include/aarch64-linux-gnu/qt5/QtQml')
            add_includedirs('/usr/include/aarch64-linux-gnu/qt5/QtNetwork')
            add_includedirs('/usr/include/aarch64-linux-gnu/qt5/QtMultimedia')
            add_includedirs('/usr/include/aarch64-linux-gnu/qt5/QtGui')
        end
    end

    -- 交叉编译时不链接 Qt 库（运行时由宿主进程提供符号）
    if is_cross() then
        set_kind('shared')
        add_ldflags('-shared', '-Wl,--no-undefined')
    else
        add_rules('qt.shared')
        add_frameworks(
            'QtCore',
            'QtQuick',
            'QtQml',
            'QtNetwork',
            'QtMultimedia',
            'QtGui'
        )
    end
