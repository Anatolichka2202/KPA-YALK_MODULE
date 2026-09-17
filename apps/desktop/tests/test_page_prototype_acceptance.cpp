#include "test_page.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
void require(bool condition,const char* message){if(!condition){std::cerr<<message<<'\n';std::exit(EXIT_FAILURE);}}
void saveScene(TestPage& page,const QString& requested,const QString& scene,const QString& base)
{
    if(base.isEmpty()||requested!=scene)return;
    for(const QSize size:{QSize(1920,1080),QSize(1600,900)}){
        page.resize(size);page.show();QApplication::processEvents();
        const QString suffix=QStringLiteral("_%1x%2.png").arg(size.width()).arg(size.height());
        require(page.grab().save(base+suffix),"cannot save prototype acceptance screenshot");
    }
}
}

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    TestPage page;
    const QString requested=qEnvironmentVariable("MILTECH_UI_SCENE",QStringLiteral("YALK"));
    const QString screenshot=qEnvironmentVariable("ORBITA_UI_SCREENSHOT");

    require(page.styleSheet().contains(QStringLiteral("#08131d")),"prototype v1.3 background token missing");
    require(page.styleSheet().contains(QStringLiteral("#2e7de9")),"prototype v1.3 blue token missing");

    auto* scope=page.findChild<QComboBox*>(QStringLiteral("testScope"));
    auto* test=page.findChild<QComboBox*>(QStringLiteral("testType"));
    auto* operatorSelector=page.findChild<QComboBox*>(QStringLiteral("productionOperatorSelector"));
    auto* registry=page.findChild<QTableWidget*>(QStringLiteral("productionRegistryTable"));
    auto* queue=page.findChild<QTableWidget*>(QStringLiteral("productionSessionTable"));
    auto* enter=page.findChild<QPushButton*>(QStringLiteral("enterPreparation"));
    auto* equipment=page.findChild<QTableWidget*>(QStringLiteral("equipmentTable"));
    auto* histogram=page.findChild<QWidget*>(QStringLiteral("yalkChannelHistogram"));
    auto* initial=page.findChild<QWidget*>(QStringLiteral("yalkInitialStateGrid"));
    auto* contacts=page.findChild<QWidget*>(QStringLiteral("yalkContactThresholdOverview"));
    auto* overload=page.findChild<QWidget*>(QStringLiteral("yalkOverloadOverview"));
    auto* ytp=page.findChild<QWidget*>(QStringLiteral("ytpChannelHistogram"));
    auto* yvp=page.findChild<QWidget*>(QStringLiteral("yvpEightChannelOverview"));
    auto* context=page.findChild<QLabel*>(QStringLiteral("frozenProcedureContext"));
    require(scope&&test&&operatorSelector&&registry&&queue&&enter&&equipment&&histogram&&initial&&contacts&&overload&&ytp&&yvp&&context,"clean prototype controls missing");

    page.setProductionMode(true);
    page.setScenarioInfo(QStringLiteral("PROD_FULL"),true,false,{},QStringLiteral("ready"));
    page.setAvailableProductionProducts({QStringLiteral("345"),QStringLiteral("346")});
    operatorSelector->addItem(QStringLiteral("Иванов И.И."),QStringLiteral("Иванов И.И."));
    operatorSelector->setCurrentIndex(operatorSelector->findData(QStringLiteral("Иванов И.И.")));
    QApplication::processEvents();
    require(registry->rowCount()==2,"registry must show two products");
    require(!enter->isEnabled(),"empty queue must block preparation");

    QPushButton* add=nullptr;
    for(auto* b:registry->findChildren<QPushButton*>())if(b->text().contains(QStringLiteral("Добавить"))){add=b;break;}
    require(add,"registry add action missing");add->click();QApplication::processEvents();
    require(queue->rowCount()==1,"registry action must add one queue item");
    require(enter->isEnabled(),"operator plus queue must enable preparation");
    saveScene(page,requested,QStringLiteral("SESSION"),screenshot);
    enter->click();QApplication::processEvents();saveScene(page,requested,QStringLiteral("PREPARATION"),screenshot);

    page.setRunInProgress(true,QStringLiteral("running"));
    orbita::stand::RunEvent power;power.nodeId="supply_range";power.stage="SUPPLY";power.data={{"setpoint_v","27"},{"volts","27.01"},{"amperes","0.238"},{"elapsed_s","10"},{"duration_s","300"}};page.setRunEvent(power);saveScene(page,requested,QStringLiteral("POWER"),screenshot);

    std::ostringstream mean,minv,maxv;
    for(int i=0;i<100;++i){if(i){mean<<',';minv<<',';maxv<<',';}double v=3.1+(i%9-4)*.003;mean<<v;minv<<v-.006;maxv<<v+.006;}
    orbita::stand::RunEvent bg;bg.nodeId="monitor";bg.stage="BACKGROUND";bg.data={{"section","YALK"},{"background_mean",mean.str()},{"background_min",minv.str()},{"background_max",maxv.str()}};page.setRunEvent(bg);

    orbita::stand::RunEvent open;open.nodeId="yalk_initial";open.stage="YALK_INITIAL";open.verdict=orbita::stand::RunVerdict::Ok;open.data={{"ulk_address","32"},{"channel_index","29"},{"channel_count","80"},{"yalk_v","-0.971"},{"signal","1"},{"expected_signal","1"}};page.setRunEvent(open);QApplication::processEvents();require(initial->property("initialMeasurementCount").toInt()==1,"open-circuit event not rendered");saveScene(page,requested,QStringLiteral("YALK_INITIAL"),screenshot);

    for(int address=1;address<=87;++address){if(!(address<=28||(address>=32&&address<=43)||(address>=45&&address<=70)||address>=74))continue;double v=3.1+(address%9-4)*.003;orbita::stand::RunEvent e;e.nodeId="yalk_channels";e.stage="MEASUREMENT";e.verdict=orbita::stand::RunVerdict::Ok;e.data={{"ulk_address",std::to_string(address)},{"command_v","3.1"},{"v7_v","3.100"},{"yalk_v",std::to_string(v)},{"lower_limit_v","3.069"},{"upper_limit_v","3.131"},{"signal","0"},{"value_samples",std::to_string(v-.004)+","+std::to_string(v)+","+std::to_string(v+.004)}};page.setRunEvent(e);}QApplication::processEvents();require(histogram->property("renderedChannelCount").toInt()==80,"YALK plane must show 80 channels");saveScene(page,requested,QStringLiteral("YALK"),screenshot);

    for(double command:{0.0,0.9,2.5})for(int address=1;address<=87;++address){if(!(address<=28||(address>=32&&address<=43)||(address>=45&&address<=70)||address>=74))continue;orbita::stand::RunEvent e;e.nodeId="yalk_contacts";e.stage="MEASUREMENT";e.verdict=orbita::stand::RunVerdict::Ok;e.data={{"ulk_address",std::to_string(address)},{"command_v",std::to_string(command)},{"v7_v",std::to_string(command)},{"yalk_v",std::to_string(command)},{"signal",command>=2.0?"1":"0"},{"value_samples",std::to_string(command)}};page.setRunEvent(e);}QApplication::processEvents();require(contacts->property("contactMeasurementCount").toInt()==240,"contact plane must retain 240 measurements");saveScene(page,requested,QStringLiteral("YALK_CONTACT"),screenshot);

    orbita::stand::RunEvent impact;impact.nodeId="yalk_overload_positive";impact.stage="OVERLOAD";impact.data={{"polarity","+12 V"},{"stressed_channel","37"},{"impact_index","37"},{"impact_count","176"},{"settle_ms","10000"}};page.setRunEvent(impact);
    for(int observed=1;observed<=88;++observed){if(observed==37)continue;double baseline=1800+(observed%13-6)*.8,delta=observed==30?3.0:(observed%5-2)*.35;orbita::stand::RunEvent e;e.nodeId="yalk_overload_positive";e.stage="MEASUREMENT";e.verdict=std::abs(delta)<=2?orbita::stand::RunVerdict::Ok:orbita::stand::RunVerdict::Fail;e.data={{"polarity","+12 V"},{"stressed_channel","37"},{"observed_channel",std::to_string(observed)},{"baseline_code",std::to_string(baseline)},{"current_code",std::to_string(baseline+delta)},{"delta_code",std::to_string(delta)},{"lower_delta_code","-2"},{"upper_delta_code","2"}};page.setRunEvent(e);}QApplication::processEvents();require(overload->property("overloadMeasurementCount").toInt()==87,"overload plane must show 87 observed channels");saveScene(page,requested,QStringLiteral("YALK_OVERLOAD"),screenshot);

    orbita::stand::RunEvent operatorEvent;operatorEvent.nodeId="ytp_channels";operatorEvent.stage="OPERATOR";operatorEvent.data={{"target_resistance_ohm","120"},{"point_index","2"},{"point_count","3"}};page.setRunEvent(operatorEvent);
    for(int channel=1;channel<=30;++channel){double measured=120+(channel%9-4)*.08;orbita::stand::RunEvent e;e.nodeId="ytp_channels";e.stage="MEASUREMENT";e.verdict=orbita::stand::RunVerdict::Ok;e.data={{"ytp_channel",std::to_string(channel)},{"actual_reference_ohm","120.000"},{"measured_resistance_ohm",std::to_string(measured)},{"value_samples",std::to_string(measured-.09)+","+std::to_string(measured)+","+std::to_string(measured+.09)}};page.setRunEvent(e);}QApplication::processEvents();require(ytp->property("renderedChannelCount").toInt()==30,"YTP plane must show 30 channels");saveScene(page,requested,QStringLiteral("YTP"),screenshot);

    for(int channel=1;channel<=8;++channel){orbita::stand::RunEvent e;e.nodeId="yvp_measurement";e.stage="YVP_V7_POINT";e.verdict=orbita::stand::RunVerdict::NotRun;e.data={{"yvp_channel",std::to_string(channel)},{"gain_mv_per_pc","1"},{"set_frequency_hz","500"},{"rigol_input_vpp","2"},{"v7_output_vrms",std::to_string(.141+channel*.001)},{"calculated_gain_mv_per_pc",std::to_string(.98+channel*.004)},{"acceptance","not_applied"}};page.setRunEvent(e);}QApplication::processEvents();require(yvp->property("yvpRenderedChannelCount").toInt()==8,"YVP plane must show eight channels");saveScene(page,requested,QStringLiteral("YVP"),screenshot);

    orbita::stand::ScenarioRunResult result;result.runId="prototype-acceptance";result.verdict=orbita::stand::RunVerdict::Incomplete;auto addStep=[&](const char* id,orbita::stand::RunVerdict verdict){orbita::stand::StepRunResult s;s.nodeId=id;s.verdict=verdict;result.steps.push_back(std::move(s));};addStep("supply_status",orbita::stand::RunVerdict::Ok);addStep("yalk_initial",orbita::stand::RunVerdict::Ok);addStep("ytp_channels",orbita::stand::RunVerdict::Ok);addStep("yvp_v7_isd",orbita::stand::RunVerdict::Incomplete);page.setRunResult(result);saveScene(page,requested,QStringLiteral("FINISH"),screenshot);

    std::cout<<"Clean prototype acceptance passed\n";return EXIT_SUCCESS;
}
