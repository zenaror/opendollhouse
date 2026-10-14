// Export selected functions without changing the program.
// @category OpenDollhouse
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
public class ODHFunctions extends GhidraScript {
    public void run() throws Exception {
        String[] args=getScriptArgs();if(args.length<2)throw new IllegalArgumentException("output dir and VAs required");
        Path directory=Path.of(args[0]);Files.createDirectories(directory);
        DecompInterface decompiler=new DecompInterface();decompiler.openProgram(currentProgram);
        try{
            for(int n=1;n<args.length;n++){
                Address address=toAddr(args[n]);Function f=currentProgram.getFunctionManager().getFunctionContaining(address);
                if(f==null){println("No analyzed function at "+address);continue;}
                StringBuilder out=new StringBuilder("program="+currentProgram.getName()+" query_va="+address+" function="+f.getName()+" entry_va="+f.getEntryPoint()+"\n");
                ReferenceIterator references=currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
                int count=0;while(references.hasNext()&&count++<256){Reference r=references.next();out.append("REF ").append(r.getFromAddress()).append(' ').append(r.getReferenceType()).append('\n');}
                InstructionIterator instructions=currentProgram.getListing().getInstructions(f.getBody(),true);
                count=0;while(instructions.hasNext()&&count++<8192){Instruction i=instructions.next();out.append(i.getAddress()).append(' ').append(i).append('\n');}
                if(instructions.hasNext())out.append("ASSEMBLY TRUNCATED AFTER 8192 INSTRUCTIONS\n");
                Files.writeString(directory.resolve(f.getEntryPoint()+".asm.txt"),out.toString());
                DecompileResults result=decompiler.decompileFunction(f,30,monitor);
                String text=result.decompileCompleted()?result.getDecompiledFunction().getC():"DECOMPILE FAILED: "+result.getErrorMessage();
                Files.writeString(directory.resolve(f.getEntryPoint()+".c.txt"),text);
                println("Exported "+f.getEntryPoint()+" for query "+address);
            }
        }finally{decompiler.dispose();}
    }
}
