#!/usr/bin/lua5.3

--[[
 QN_FLAGS                26     //  Symbol:   GArdKMSuDNLmoTnsORWteFXxlv
 FLAG_GETATTR           0x1     //  G
 FLAG_ACCESS            0x2     //  A
 FLAG_READLINK          0x4     //  r
 FLAG_READDIR           0x8     //  d
 FLAG_MKNOD            0x10     //  K
 FLAG_MKDIR            0x20     //  M
 FLAG_SYMLINK          0x40     //  S
 FLAG_UNLINK           0x80     //  u
 FLAG_RMDIR           0x100     //  D
 FLAG_RENAME          0x200     //  N
 FLAG_LINK            0x400     //  L
 FLAG_CHMOD           0x800     //  m
 FLAG_CHOWN          0x1000     //  o
 FLAG_TRUNCATE       0x2000     //  T
 FLAG_UTIME          0x4000     //  n
 FLAG_UTIMENS        0x8000     //  s
 FLAG_OPEN          0x10000     //  O
 FLAG_READ          0x20000     //  R
 FLAG_WRITE         0x40000     //  W
 FLAG_STATFS        0x80000     //  t
 FLAG_RELEASE      0x100000     //  e
 FLAG_FSYNC        0x200000     //  F
 FLAG_SETXATTR     0x400000     //  X
 FLAG_GETXATTR     0x800000     //  x
 FLAG_LISTXATTR   0x1000000     //  l
 FLAG_REMOVEXATTR 0x2000000     //  v
--]]

function get_dir(str)
    sep='/'
    return str:match("(.*"..sep..")")
end

function get_extension(url)
    pointed_ext=url:match("^.+(%..+)$")
    if pointed_ext~=nil then
        return string.sub(pointed_ext,2)
    else
        return ""
    end
end

function is_extension(path, list)
    if path~=nil then
        ext = get_extension(path)
        for i=1,#list do
            if ext==list[i] then
                return true
            end
        end
    end
    return false
end

function string:split(delimiter)
    local result = { }
    local from  = 1
    local delim_from, delim_to = string.find( self, delimiter, from)
    while delim_from do
        table.insert( result, string.sub(self, from, delim_from-1))
        from  = delim_to + 1
        delim_from, delim_to = string.find(self, delimiter, from)
    end
    table.insert(result, string.sub( self, from))
    return result
end

function process_path(path, flags, from, to)
    dir=get_dir(path)
    if dir==nil then
        return
    end
    to_dir=to..dir
    from_path=from..path
    to_path=to..path

    if (string.find(flags, 'O')~=nil or string.find(flags, 'R')~=nil or string.find(flags, 'r')~=nil) then
        -- Really opened files            
        if string.len(dir)>1 then
            print('mkdir -p "'..to_dir..'"')
        end 
        print('cp -a "'..from_path..'" "'..to_path..'"')
    elseif (string.find(flags, 'G')~=nil) then
        if (string.sub(dir,1,7)~="/prebuilts") then
            -- Only in vendor
            if (string.find(flags, 'd')~=nil) then
                print('mkdir -p "'..to_path..'"')
            else
                print('mkdir -p "'..to_dir..'"')
                print('touch "'..to_path..'"')
            end
        end
    end
end

-- Ok, go
if #arg<3 then
    print("Usage:\n    mini_mirror <list file> <from> <to>");
    os.exit(1)
end

file_list=arg[1]
from=arg[2]
to=arg[3]

count=0
for line in io.lines(file_list) do
    if string.sub(line,1,1) ~= "#" then
        params=string.split(line, ':')
        if #params~=3 then
            print("!!!! Line ", line, "has wrong format!")
        else
            path=params[3]
            flags=params[1]
            process_path(path, flags, from, to)
            count=count+1
            if math.fmod(count,100)==0 then
                print('echo "'..count..' lines processed."')
            end
        end
    end
end
