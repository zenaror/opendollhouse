// @category OpenDollhouse
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.util.*;
public class ODHStringRefs extends GhidraScript {
    public void run() throws Exception {
        String[] args=getScriptArgs();StringBuilder out=new StringBuilder();
        Set<String> functions=new TreeSet<>();
        for(int n=1;n<args.length;n++){
            Address address=toAddr(args[n]);ReferenceIterator refs=currentProgram.getReferenceManager().getReferencesTo(address);
            while(refs.hasNext()){
                Reference r=refs.next();Function f=currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
                out.append(address).append(" <- ").append(r.getFromAddress()).append(' ').append(r.getReferenceType()).append(" function=").append(f==null?"none":f.getEntryPoint()).append('\n');
                if(f!=null)functions.add(f.getEntryPoint().toString());
            }
        }
        out.append("FUNCTIONS ").append(String.join(" ",functions)).append('\n');Files.writeString(Path.of(args[0]),out.toString());println(out.toString());
    }
}
