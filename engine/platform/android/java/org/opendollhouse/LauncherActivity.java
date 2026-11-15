/* SPDX-License-Identifier: MPL-2.0 */
package org.opendollhouse;
import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
import android.widget.*;
import java.io.File;

/** Startup settings only; the game UI remains the original single canvas. */
public class LauncherActivity extends Activity {
    private static final String[] CODES={"en-US","en-GB","fr","de","it","es","pt-BR"};
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        File external=getExternalFilesDir(null);
        LinearLayout box=new LinearLayout(this);box.setOrientation(LinearLayout.VERTICAL);box.setPadding(32,16,32,16);
        TextView text=new TextView(this);
        File game=external==null ? null : new File(external,"game");
        if(game!=null)game.mkdirs();
        text.setText("OpenDollhouse — experimental\nCopy your own installed game data into:\n"+(game==null ? "External app storage unavailable" : game.getAbsolutePath())+"\nThe folder must contain GameData and UserData. No game files are included. Saves are kept separately.\nTouch or mouse input; controller mapping is not finished.");
        box.addView(text);
        Spinner language=new Spinner(this);language.setAdapter(new ArrayAdapter<String>(this,android.R.layout.simple_spinner_dropdown_item,CODES));
        String saved=getPreferences(0).getString("language","en-US");
        for(int i=0;i<CODES.length;i++)if(CODES[i].equals(saved))language.setSelection(i);
        box.addView(language);
        Button play=new Button(this);play.setText("Play");box.addView(play);
        play.setOnClickListener(v->{
            if(game==null || !new File(game,"GameData").isDirectory() || !new File(game,"UserData").isDirectory()) {
                Toast.makeText(this,"Copy GameData and UserData into the displayed game folder first.",Toast.LENGTH_LONG).show();return;
            }
            String code=CODES[language.getSelectedItemPosition()];getPreferences(0).edit().putString("language",code).apply();
            startActivity(new Intent(this,GameActivity.class).putExtra("language",code));
        });
        ScrollView scroll=new ScrollView(this);scroll.addView(box);setContentView(scroll);
    }
}
