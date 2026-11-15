/* SPDX-License-Identifier: MPL-2.0 */
package org.opendollhouse;
import org.libsdl.app.SDLActivity;
import java.io.File;

public class GameActivity extends SDLActivity {
    @Override protected String[] getLibraries(){ return new String[]{"SDL2","iconv","main"}; }
    @Override protected String[] getArguments(){
        File data=new File(getExternalFilesDir(null),"game");
        File saves=new File(getFilesDir(),"saves");saves.mkdirs();
        File cache=new File(getCacheDir(),"engine");cache.mkdirs();
        String code=getIntent().getStringExtra("language");
        return new String[]{"--data-root",data.getAbsolutePath(),"--save-root",saves.getAbsolutePath(),"--cache-dir",cache.getAbsolutePath(),"--language",code==null?"en-US":code};
    }
}
