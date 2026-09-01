// Ghidra headless helper for the stripped KPA Rokot executable.
// Prints string references and decompiles the nearest functions for YTP/ULK code.

import java.util.LinkedHashSet;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class GhidraDumpYtp extends GhidraScript {
    private static final String[] NEEDLES = {
        "УЛКрежимРС",
        "ЯТП_РС",
        "ЯТП854",
        "ClCmdULKmodeRS",
        "ClSetupYATP_RS",
        "ClIoInterfaceRecorder",
        "on_tmrYTP",
        "EthRec",
        "YTP_ETH1",
        "контроль",
        "термопара",
        "Ом1",
        "Ом2",
        "Ом3",
        "Ом4",
        "Ом5",
        "4-140",
        "4-240",
        "3-140",
        "3-240"
    };

    @Override
    public void run() throws Exception {
        Set<Function> functions = new LinkedHashSet<>();

        DataIterator dataIterator = currentProgram.getListing().getDefinedData(true);
        while (dataIterator.hasNext()) {
            Data data = dataIterator.next();
            Object value = data.getValue();
            if (!(value instanceof String)) {
                continue;
            }

            String text = (String) value;
            if (!matches(text)) {
                continue;
            }

            println("YTP_STRING " + data.getAddress() + " " + quote(text));
            ReferenceIterator refs = currentProgram.getReferenceManager()
                .getReferencesTo(data.getAddress());

            while (refs.hasNext()) {
                Reference ref = refs.next();
                Function function = currentProgram.getFunctionManager()
                    .getFunctionContaining(ref.getFromAddress());
                println("  REF " + ref.getFromAddress()
                    + " type=" + ref.getReferenceType()
                    + " function=" + format(function));
                if (function != null) {
                    functions.add(function);
                }
            }
        }

        // UTF-8 Cyrillic strings are not always defined as Data by the PE
        // analyzers. These addresses are mapped from their raw file offsets.
        collectReferences(functions, toAddr("0138df58"), "ЯТП_РС#1");
        collectReferences(functions, toAddr("013b13a9"), "ЯТП_РС#2");
        collectReferences(functions, toAddr("013b1420"), "ЯТП_РС#3");

        addFunction(functions, toAddr("005dd9e0"));
        addFunction(functions, toAddr("005de190"));

        // Keep the output deliberately narrow. Walking all callers/callees of
        // the static Qt binary produces megabytes of unrelated decompilation.
        Set<Function> decompileTargets = new LinkedHashSet<>();
        addFunction(decompileTargets, toAddr("005c66c0"));

        FunctionIterator nearby = currentProgram.getFunctionManager()
            .getFunctions(toAddr("005dca00"), true);
        while (nearby.hasNext()) {
            Function function = nearby.next();
            if (function.getEntryPoint().compareTo(toAddr("005dd420")) >= 0) {
                break;
            }
            println("YTP_NEARBY_FUNCTION " + format(function));
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try {
            for (Function function : decompileTargets) {
                println("\n===== YTP_FUNCTION " + format(function) + " =====");
                DecompileResults result = decompiler.decompileFunction(
                    function, 120, monitor);
                if (result.decompileCompleted()) {
                    println(result.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILE_FAILED " + result.getErrorMessage());
                }
            }
        } finally {
            decompiler.dispose();
        }
    }

    private void collectReferences(
        Set<Function> functions,
        Address address,
        String label) {
        println("YTP_ADDRESS " + label + " " + address);
        ReferenceIterator refs = currentProgram.getReferenceManager()
            .getReferencesTo(address);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            Function function = currentProgram.getFunctionManager()
                .getFunctionContaining(ref.getFromAddress());
            println("  REF " + ref.getFromAddress()
                + " type=" + ref.getReferenceType()
                + " function=" + format(function));
            if (function != null) {
                functions.add(function);
            }
        }
    }

    private void addFunction(Set<Function> functions, Address address) {
        Function function = currentProgram.getFunctionManager().getFunctionAt(address);
        if (function == null) {
            function = createFunction(address, null);
        }
        println("YTP_FIXED_FUNCTION " + address + " " + format(function));
        if (function != null) {
            functions.add(function);
        }
    }

    private static boolean matches(String text) {
        for (String needle : NEEDLES) {
            if (text.contains(needle)) {
                return true;
            }
        }
        return false;
    }

    private static String format(Function function) {
        return function == null
            ? "<none>"
            : function.getName() + "@" + function.getEntryPoint();
    }

    private static String quote(String text) {
        return '"' + text.replace("\n", "\\n").replace("\r", "\\r") + '"';
    }
}
