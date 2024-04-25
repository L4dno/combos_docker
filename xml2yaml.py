prefix = ""
only_once = []
    
with open("parameters.xml", "r") as xml_file:
    with open("parameters.yaml", "w") as yaml_file:

        for line in xml_file.readlines():
            line =line.strip() 
            if line == "" or line.startswith("<!--"):
                continue
            elif '/>' in line:
                prefix = prefix[:-4]
                continue
            elif '</' not in line:
                param_name = line.split('<')[1].split('>')[0] 
                if param_name in only_once:
                    prefix += '    '
                    if param_name == 'group' and 'gproject' in only_once:
                        only_once.remove('gproject')
                    continue
                if param_name in ["sproject", "group", "gproject"]:
                    only_once.append(param_name)
                    param_name += 's'
                print(prefix, param_name, ":", file=yaml_file, sep="")
                prefix += '    '
                continue
           
            param_name,value = line.split('<')[1].split('>')
            if param_name in ["snumber", "n_clients", "pnumber"]:
                print(prefix[:-2], "- ", param_name, ": ",value, file=yaml_file, sep="")
            else:                
                print(prefix, param_name, ": ",value, file=yaml_file, sep="")